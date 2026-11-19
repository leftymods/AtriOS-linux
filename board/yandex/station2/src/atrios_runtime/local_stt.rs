use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_float, c_int, c_void};
use std::path::Path;
use std::sync::{Mutex, OnceLock};

use crate::json::Json;
use crate::sys::{dlopen, dlsym, RTLD_GLOBAL, RTLD_NOW};

const VOSK_LIB_PATHS: &[&str] = &[
    "/usr/lib/atrios/libvosk.so",
    "/usr/lib/libvosk.so",
    "/usr/lib/aarch64-linux-gnu/libvosk.so",
];

const ATOMIC_LIB_PATHS: &[&str] = &[
    "/usr/lib/atrios/libatomic.so.1",
    "/usr/lib/aarch64-linux-gnu/libatomic.so.1",
    "libatomic.so.1",
];

const MODEL_PATHS: &[&str] = &[
    "/usr/share/atrios/models/vosk-model-small-ru",
    "/var/lib/atrios/models/vosk-model-small-ru",
];

#[allow(non_snake_case)]
struct VoskFns {
    vosk_set_log_level: unsafe extern "C" fn(c_int),
    vosk_model_new: unsafe extern "C" fn(*const c_char) -> *mut c_void,
    vosk_recognizer_new: unsafe extern "C" fn(*mut c_void, c_float) -> *mut c_void,
    vosk_recognizer_accept_waveform: unsafe extern "C" fn(*mut c_void, *const c_char, c_int) -> c_int,
    vosk_recognizer_final_result: unsafe extern "C" fn(*mut c_void) -> *const c_char,
    vosk_recognizer_reset: unsafe extern "C" fn(*mut c_void),
}

struct VoskEngine {
    fns: VoskFns,
    _model: *mut c_void,
    rec: *mut c_void,
}

unsafe impl Send for VoskEngine {}

static ENGINE: OnceLock<Option<Mutex<VoskEngine>>> = OnceLock::new();

unsafe fn sym<T>(handle: *mut c_void, name: &str) -> Result<T, String> {
    let cname = CString::new(name).unwrap();
    let ptr = dlsym(handle, cname.as_ptr());
    if ptr.is_null() {
        return Err(format!("Missing Vosk symbol {name}"));
    }
    Ok(std::mem::transmute_copy(&ptr))
}

fn find_model_dir() -> Option<&'static str> {
    for &p in MODEL_PATHS {
        if Path::new(p).is_dir() && Path::new(p).join("conf").is_dir() {
            return Some(p);
        }
    }
    None
}

fn load_engine() -> Option<Mutex<VoskEngine>> {
    let model_path = find_model_dir()?;
    unsafe {
        for &a in ATOMIC_LIB_PATHS {
            if let Ok(ca) = CString::new(a) {
                let h = dlopen(ca.as_ptr(), RTLD_NOW | RTLD_GLOBAL);
                if !h.is_null() {
                    break;
                }
            }
        }
        let mut handle = std::ptr::null_mut();
        for &lp in VOSK_LIB_PATHS {
            if !Path::new(lp).exists() {
                continue;
            }
            if let Ok(cl) = CString::new(lp) {
                handle = dlopen(cl.as_ptr(), RTLD_NOW | RTLD_GLOBAL);
                if !handle.is_null() {
                    break;
                }
            }
        }
        if handle.is_null() {
            eprintln!("[local-stt] libvosk.so not loaded");
            return None;
        }
        let fns = match (|| -> Result<VoskFns, String> {
            Ok(VoskFns {
                vosk_set_log_level: sym(handle, "vosk_set_log_level")?,
                vosk_model_new: sym(handle, "vosk_model_new")?,
                vosk_recognizer_new: sym(handle, "vosk_recognizer_new")?,
                vosk_recognizer_accept_waveform: sym(handle, "vosk_recognizer_accept_waveform")?,
                vosk_recognizer_final_result: sym(handle, "vosk_recognizer_final_result")?,
                vosk_recognizer_reset: sym(handle, "vosk_recognizer_reset")?,
            })
        })() {
            Ok(f) => f,
            Err(e) => {
                eprintln!("[local-stt] {e}");
                return None;
            }
        };
        (fns.vosk_set_log_level)(-1);
        let c_model = CString::new(model_path).ok()?;
        let model = (fns.vosk_model_new)(c_model.as_ptr());
        if model.is_null() {
            eprintln!("[local-stt] failed loading model at {model_path}");
            return None;
        }
        let rec = (fns.vosk_recognizer_new)(model, 16000.0);
        if rec.is_null() {
            eprintln!("[local-stt] failed creating Vosk recognizer");
            return None;
        }
        println!("[local-stt] Vosk Russian offline model loaded ({model_path})");
        Some(Mutex::new(VoskEngine {
            fns,
            _model: model,
            rec,
        }))
    }
}

pub fn init() -> bool {
    ENGINE.get_or_init(load_engine).is_some()
}

pub fn is_available() -> bool {
    ENGINE.get().map(|o| o.is_some()).unwrap_or(false)
}

fn condition_pcm16_for_vosk(pcm: &[u8]) -> Vec<u8> {
    let pad_samples = 1280usize; // 80 ms @ 16 kHz
    let samples: Vec<i16> = pcm
        .chunks_exact(2)
        .map(|b| i16::from_le_bytes([b[0], b[1]]))
        .collect();
    if samples.is_empty() {
        return Vec::new();
    }
    let mean = samples.iter().map(|&s| s as f64).sum::<f64>() / (samples.len() as f64);
    let mut peak = 0.0f64;
    let mut centered = Vec::with_capacity(samples.len());
    for &s in &samples {
        let v = (s as f64) - mean;
        let av = v.abs();
        if av > peak {
            peak = av;
        }
        centered.push(v);
    }
    let gain = if (350.0..14000.0).contains(&peak) {
        (14000.0 / peak).min(4.0)
    } else {
        1.0
    };
    let mut out = vec![0u8; (pad_samples * 2 + centered.len()) * 2];
    for (idx, v) in centered.into_iter().enumerate() {
        let s = (v * gain).round().clamp(-32767.0, 32767.0) as i16;
        let off = (pad_samples + idx) * 2;
        out[off..off + 2].copy_from_slice(&s.to_le_bytes());
    }
    out
}

pub fn transcribe_pcm16(pcm: &[u8]) -> Result<String, String> {
    let eng_opt = ENGINE.get_or_init(load_engine);
    let Some(ref mtx) = eng_opt else {
        return Err("Local Vosk STT not available".into());
    };
    let conditioned = condition_pcm16_for_vosk(pcm);
    if conditioned.is_empty() {
        return Ok(String::new());
    }
    let guard = mtx.lock().map_err(|_| "Local STT mutex poisoned")?;
    unsafe {
        (guard.fns.vosk_recognizer_reset)(guard.rec);
        let mut off = 0usize;
        while off < conditioned.len() {
            let chunk = (conditioned.len() - off).min(6400);
            let rc = (guard.fns.vosk_recognizer_accept_waveform)(
                guard.rec,
                conditioned[off..].as_ptr() as *const c_char,
                chunk as c_int,
            );
            if rc < 0 {
                return Err("Vosk accept_waveform error".into());
            }
            off += chunk;
        }
        let raw_ptr = (guard.fns.vosk_recognizer_final_result)(guard.rec);
        if raw_ptr.is_null() {
            return Ok(String::new());
        }
        let json_str = CStr::from_ptr(raw_ptr).to_string_lossy().into_owned();
        if let Ok(j) = Json::parse(&json_str) {
            if let Some(t) = j.get_str("text") {
                return Ok(t.trim().to_string());
            }
        }
        Ok(String::new())
    }
}
