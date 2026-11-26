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

// Fast finite-state grammar for local commands & wake proxies (~5-8x faster than unconstrained decoding,
// shares the single loaded VoskModel in RAM with zero extra memory).
const CMD_GRAMMAR: &str = r#"[
  "арти", "атриум", "астры", "натрий", "а три",
  "привет", "слушай", "эй",
  "блютуз", "блютус", "колонка", "колонку", "колонки", "сопряжение", "поиск", "подключи", "отключи", "видимость",
  "включи", "включить", "вруби", "запусти", "активируй", "открой",
  "выключи", "выключить", "отключить", "выруби", "убери", "закрой",
  "поставь", "поставить", "установи", "установить", "заведи", "завести",
  "отмени", "отменить", "удали", "удалить", "сбрось", "сбросить", "останови", "остановить",
  "покажи", "скажи", "подскажи", "сколько", "какой", "какое", "какая", "сделай",
  "добавь", "прибавь", "убавь", "уменьши", "увеличь", "переключи", "верни",
  "продолжи", "продолжай", "возобнови", "играй", "сними",
  "стоп", "хватит", "отмена", "завались", "замолчи", "заткнись", "тихо", "молчи",
  "пока", "до", "свидания", "достаточно", "не", "надо", "спасибо",
  "громче", "погромче", "тише", "потише", "громкость", "звук", "на",
  "чуть", "немного", "слегка", "сильно", "намного", "минимум", "половину", "среднюю", "максимум", "полную",
  "вверх", "вниз", "процентов", "процент", "процента",
  "трек", "песню", "песня", "музыку", "вперед", "дальше", "назад",
  "следующий", "следующая", "следующую", "предыдущий", "предыдущая", "предыдущую", "прошлый", "другой", "другую",
  "пауза", "паузу", "паузы",
  "яндекс", "яндекса", "мою", "моя", "волну", "волна", "любимые", "любимую", "треки",
  "лайк", "лайкни", "нравится", "дизлайк", "играет", "сейчас", "что", "мне",
  "таймер", "таймеры", "таймеров", "будильник", "будильники", "будильников",
  "разбуди", "разбудить", "меня", "через", "в", "во",
  "секунд", "секунду", "секунды", "минут", "минуту", "минуты", "полминуты",
  "час", "часа", "часов", "полчаса", "полтора", "утра", "дня", "вечера", "ночи",
  "время", "времени", "который", "дата", "число", "день", "недели", "сегодня", "завтра",
  "осталось", "активные", "все", "список", "какие", "есть", "оставь", "оставить", "закрепи", "убирай", "первый", "второй", "третий", "экране", "экрана",
  "подсветку", "подсветка", "кольцо", "свет", "экран", "дисплей", "часы", "глаза", "глазки",
  "погоду", "погода", "красный", "синий", "зеленый", "зеленая", "желтый", "белый",
  "фиолетовый", "розовый", "голубой", "оранжевый", "радуга", "радугу", "ночник", "режим",
  "ноль", "один", "одну", "одна", "два", "две", "три", "четыре", "пять", "шесть", "семь", "восемь", "девять",
  "десять", "одиннадцать", "двенадцать", "тринадцать", "четырнадцать", "пятнадцать",
  "шестнадцать", "семнадцать", "восемнадцать", "девятнадцать",
  "двадцать", "тридцать", "сорок", "пятьдесят", "шестьдесят", "семьдесят", "восемьдесят", "девяносто", "сто",
  "половина", "четверть",
  "[unk]"
]"#;

#[allow(non_snake_case)]
struct VoskFns {
    vosk_set_log_level: unsafe extern "C" fn(c_int),
    vosk_model_new: unsafe extern "C" fn(*const c_char) -> *mut c_void,
    vosk_recognizer_new: unsafe extern "C" fn(*mut c_void, c_float) -> *mut c_void,
    vosk_recognizer_new_grm: Option<unsafe extern "C" fn(*mut c_void, c_float, *const c_char) -> *mut c_void>,
    vosk_recognizer_accept_waveform: unsafe extern "C" fn(*mut c_void, *const c_char, c_int) -> c_int,
    vosk_recognizer_final_result: unsafe extern "C" fn(*mut c_void) -> *const c_char,
    vosk_recognizer_partial_result: Option<unsafe extern "C" fn(*mut c_void) -> *const c_char>,
    vosk_recognizer_reset: unsafe extern "C" fn(*mut c_void),
}

struct VoskEngine {
    fns: VoskFns,
    _model: *mut c_void,
    rec: *mut c_void,
    cmd_rec: *mut c_void,
    cmd_prev_sample: f64,
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

unsafe fn sym_opt<T>(handle: *mut c_void, name: &str) -> Option<T> {
    let cname = CString::new(name).ok()?;
    let ptr = dlsym(handle, cname.as_ptr());
    if ptr.is_null() {
        return None;
    }
    Some(std::mem::transmute_copy(&ptr))
}

fn find_model_dir() -> Option<&'static str> {
    for &p in MODEL_PATHS {
        if Path::new(p).is_dir() && Path::new(p).join("conf").is_dir() {
            return Some(p);
        }
    }
    None
}

pub fn npu_device_available() -> bool {
    crate::npu::is_hardware_ready() || Path::new("/dev/dri/renderD128").exists()
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
                vosk_recognizer_new_grm: sym_opt(handle, "vosk_recognizer_new_grm"),
                vosk_recognizer_accept_waveform: sym(handle, "vosk_recognizer_accept_waveform")?,
                vosk_recognizer_final_result: sym(handle, "vosk_recognizer_final_result")?,
                vosk_recognizer_partial_result: sym_opt(handle, "vosk_recognizer_partial_result"),
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
        let mut cmd_rec = std::ptr::null_mut();
        if let Some(new_grm) = fns.vosk_recognizer_new_grm {
            if let Ok(c_grm) = CString::new(CMD_GRAMMAR) {
                cmd_rec = new_grm(model, 16000.0, c_grm.as_ptr());
            }
        }
        println!(
            "[local-stt] Vosk Russian offline model loaded ({model_path}, fast_grammar={}, npu_dev={})",
            !cmd_rec.is_null(),
            npu_device_available()
        );
        Some(Mutex::new(VoskEngine {
            fns,
            _model: model,
            rec,
            cmd_rec,
            cmd_prev_sample: 0.0,
        }))
    }
}

pub fn init() -> bool {
    ENGINE.get_or_init(load_engine).is_some()
}

pub fn is_available() -> bool {
    ENGINE.get().map(|o| o.is_some()).unwrap_or(false)
}

/// Reset the constrained recognizer before starting a live wake-word segment.
pub fn reset_command_stream() -> bool {
    let Some(Some(mtx)) = ENGINE.get().map(|o| o.as_ref()) else {
        return false;
    };
    let Ok(mut guard) = mtx.lock() else {
        return false;
    };
    if guard.cmd_rec.is_null() || guard.fns.vosk_recognizer_partial_result.is_none() {
        return false;
    }
    unsafe { (guard.fns.vosk_recognizer_reset)(guard.cmd_rec) };
    guard.cmd_prev_sample = 0.0;
    true
}

/// Feed one live PCM chunk and return Vosk's current partial command transcript.
pub fn command_stream_partial(pcm: &[u8]) -> Option<String> {
    let Some(Some(mtx)) = ENGINE.get().map(|o| o.as_ref()) else {
        return None;
    };
    let mut guard = mtx.lock().ok()?;
    if guard.cmd_rec.is_null() {
        return None;
    }
    let partial_result = guard.fns.vosk_recognizer_partial_result?;
    let samples = pcm.chunks_exact(2)
        .map(|b| i16::from_le_bytes([b[0], b[1]]) as f64)
        .collect::<Vec<_>>();
    if samples.is_empty() {
        return None;
    }
    let mean = samples.iter().sum::<f64>() / samples.len() as f64;
    let mut conditioned = Vec::with_capacity(samples.len() * 2);
    let mut peak = 0.0f64;
    let mut values = Vec::with_capacity(samples.len());
    for sample in samples {
        let raw = sample - mean;
        let emph = raw - 0.90 * guard.cmd_prev_sample;
        guard.cmd_prev_sample = raw;
        let value = 0.55 * raw + 0.45 * emph;
        peak = peak.max(value.abs());
        values.push(value);
    }
    let gain = if (80.0..14000.0).contains(&peak) {
        (14000.0 / peak).min(8.0)
    } else {
        1.0
    };
    for value in values {
        let sample = (value * gain).round().clamp(-32767.0, 32767.0) as i16;
        conditioned.extend_from_slice(&sample.to_le_bytes());
    }
    let accepted = unsafe {
        (guard.fns.vosk_recognizer_accept_waveform)(
            guard.cmd_rec,
            conditioned.as_ptr() as *const c_char,
            conditioned.len() as c_int,
        )
    };
    if accepted < 0 {
        return None;
    }
    let raw_ptr = unsafe { partial_result(guard.cmd_rec) };
    if raw_ptr.is_null() {
        return Some(String::new());
    }
    let json = unsafe { CStr::from_ptr(raw_ptr) }.to_string_lossy();
    Some(Json::parse(&json).ok()?.get_str("partial")?.trim().to_string())
}

/// Finish the live constrained-recognizer pass and return its final transcript.
/// This avoids decoding the same wake-word utterance from scratch after VAD ends.
pub fn finish_command_stream() -> Option<(String, bool)> {
    let Some(Some(mtx)) = ENGINE.get().map(|o| o.as_ref()) else {
        return None;
    };
    let mut guard = mtx.lock().ok()?;
    if guard.cmd_rec.is_null() {
        return None;
    }
    let raw_ptr = unsafe { (guard.fns.vosk_recognizer_final_result)(guard.cmd_rec) };
    let raw = if raw_ptr.is_null() {
        String::new()
    } else {
        let json = unsafe { CStr::from_ptr(raw_ptr) }.to_string_lossy();
        Json::parse(&json).ok()?.get_str("text")?.trim().to_string()
    };
    unsafe { (guard.fns.vosk_recognizer_reset)(guard.cmd_rec) };
    guard.cmd_prev_sample = 0.0;
    let mut had_unk = false;
    let mut clean = Vec::new();
    for word in raw.split_whitespace() {
        if word == "[unk]" { had_unk = true; } else { clean.push(word); }
    }
    Some((clean.join(" "), had_unk))
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
    let mut prev = 0.0f64;
    for &s in &samples {
        let raw = (s as f64) - mean;
        // Blend 55% raw + 45% speech pre-emphasis (y[n] = x[n] - 0.90*x[n-1]) to suppress
        // residual low-frequency music bass while keeping speech formants crisp for Vosk.
        let emph = raw - 0.90 * prev;
        prev = raw;
        let v = 0.55 * raw + 0.45 * emph;
        let av = v.abs();
        if av > peak {
            peak = av;
        }
        centered.push(v);
    }
    let gain = if (80.0..14000.0).contains(&peak) {
        (14000.0 / peak).min(8.0)
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

unsafe fn decode_with_rec(fns: &VoskFns, rec: *mut c_void, conditioned: &[u8]) -> Result<String, String> {
    (fns.vosk_recognizer_reset)(rec);
    let mut off = 0usize;
    while off < conditioned.len() {
        let chunk = (conditioned.len() - off).min(6400);
        let rc = (fns.vosk_recognizer_accept_waveform)(
            rec,
            conditioned[off..].as_ptr() as *const c_char,
            chunk as c_int,
        );
        if rc < 0 {
            return Err("Vosk accept_waveform error".into());
        }
        off += chunk;
    }
    let raw_ptr = (fns.vosk_recognizer_final_result)(rec);
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

pub fn transcribe_command_pcm16(pcm: &[u8]) -> Result<(String, bool), String> {
    let eng_opt = ENGINE.get_or_init(load_engine);
    let Some(ref mtx) = eng_opt else {
        return Err("Local Vosk STT not available".into());
    };
    let conditioned = condition_pcm16_for_vosk(pcm);
    if conditioned.is_empty() {
        return Ok((String::new(), false));
    }
    let guard = mtx.lock().map_err(|_| "Local STT mutex poisoned")?;
    let target_rec = if !guard.cmd_rec.is_null() {
        guard.cmd_rec
    } else {
        guard.rec
    };
    let raw = unsafe { decode_with_rec(&guard.fns, target_rec, &conditioned)? };
    let mut had_unk = false;
    let mut clean_words = Vec::new();
    for tok in raw.split_whitespace() {
        if tok == "[unk]" {
            had_unk = true;
        } else {
            clean_words.push(tok);
        }
    }
    Ok((clean_words.join(" "), had_unk))
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
    unsafe { decode_with_rec(&guard.fns, guard.rec, &conditioned) }
}
