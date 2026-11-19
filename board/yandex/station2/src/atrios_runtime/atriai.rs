use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::io::{Read, Write};
use std::net::UdpSocket;
use std::os::raw::c_int;
use std::os::unix::io::AsRawFd;
use std::os::unix::net::UnixDatagram;
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicUsize, Ordering};
use std::sync::{mpsc, Mutex, OnceLock};
use std::thread;
use std::time::{Duration, Instant};

use crate::https::{migrate_legacy_url, request_bytes, request_json, require_https};
use crate::json::{obj, Json};
use crate::light::{RING_OVERRIDE_FILE, SCREEN_MODE_FILE, VOICE_LEVEL_FILE};
use crate::status::{error_kind_from_msg, set_error};
use crate::sys::{
    atomic_write_bytes, kill, now_unix_i64, poll, recv_or_read, signal, PollFd, POLLIN, SIGUSR1,
    SIGUSR2,
};

const SR: usize = 16000;
const CAP_SR: usize = 48000;
const CAP_CH: usize = 8;
const FRAME_SAMPLES: usize = 512;
const FRAME_BYTES: usize = FRAME_SAMPLES * 2;
const CAP_FRAME_BYTES: usize = FRAME_SAMPLES * 3 * CAP_CH * 4;

const AEC_BIN: &str = "/usr/lib/atrios/atri-aec";
pub const CONFIG_FILE: &str = "/var/lib/atrios/ai.json";
const STATUS_FILE: &str = "/var/run/atrios/ai_status.json";
const BUTTONS_FILE: &str = "/var/run/atrios/buttons.json";
const TRIGGER_FILE: &str = "/var/run/atrios/ai_trigger";
const PID_FILE: &str = "/var/run/atrios/atriai.pid";
const BT_CMD_SOCK: &str = "/var/run/atrios/bt_cmd.sock";
const VOLUME_FILE: &str = "/var/lib/atrios/volume";

static BUTTON_TRIGGERED: AtomicBool = AtomicBool::new(false);
static WAKE_TRIGGERED: AtomicBool = AtomicBool::new(false);
static RESUME_VISUAL: AtomicBool = AtomicBool::new(false);
static CAPTURE_FD: AtomicI32 = AtomicI32::new(-1);
static CAPTURE_FRAME_BYTES: AtomicUsize = AtomicUsize::new(FRAME_BYTES);

extern "C" fn on_sigusr1(_sig: c_int) {
    BUTTON_TRIGGERED.store(true, Ordering::SeqCst);
}
extern "C" fn on_sigusr2(_sig: c_int) {
    RESUME_VISUAL.store(true, Ordering::SeqCst);
}

#[derive(Clone)]
pub struct AiConfig {
    pub enabled: bool,
    pub server: String,
    pub ca_file: String,
    pub token: String,
    pub wake_word: String,
    pub vad_threshold: f64,
    pub silence_ms: f64,
    pub min_speech_ms: f64,
}

impl Default for AiConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            server: String::new(),
            ca_file: String::new(),
            token: String::new(),
            wake_word: "Atri".into(),
            vad_threshold: 320.0,
            silence_ms: 550.0,
            min_speech_ms: 260.0,
        }
    }
}

pub fn load_config() -> AiConfig {
    let mut cfg = AiConfig::default();
    if let Ok(txt) = fs::read_to_string(CONFIG_FILE) {
        if let Ok(j) = Json::parse(&txt) {
            if let Some(b) = j.get("enabled").and_then(|v| v.as_bool()) {
                cfg.enabled = b;
            }
            if let Some(s) = j.get_str("server") {
                cfg.server = migrate_legacy_url(s);
            }
            if let Some(s) = j.get_str("ca_file") {
                cfg.ca_file = s.to_string();
            }
            if let Some(s) = j.get_str("token") {
                cfg.token = s.to_string();
            }
            if let Some(s) = j.get_str("wake_word") {
                cfg.wake_word = s.to_string();
            }
            if let Some(n) = j.get("vad_threshold").and_then(|v| v.as_f64()) {
                cfg.vad_threshold = n;
            }
            if let Some(n) = j.get("silence_ms").and_then(|v| v.as_f64()) {
                cfg.silence_ms = n;
            }
            if let Some(n) = j.get("min_speech_ms").and_then(|v| v.as_f64()) {
                cfg.min_speech_ms = n;
            }
        }
    }
    if cfg.vad_threshold >= 600.0 {
        cfg.vad_threshold = 320.0;
    }
    if cfg.min_speech_ms >= 380.0 {
        cfg.min_speech_ms = 260.0;
    }
    cfg
}

pub fn save_config(cfg: &AiConfig) -> Result<(), String> {
    let j = obj(vec![
        ("enabled", Json::Bool(cfg.enabled)),
        ("server", Json::Str(cfg.server.clone())),
        ("ca_file", Json::Str(cfg.ca_file.clone())),
        ("token", Json::Str(cfg.token.clone())),
        ("wake_word", Json::Str(cfg.wake_word.clone())),
        ("vad_threshold", Json::Num(cfg.vad_threshold)),
        ("silence_ms", Json::Num(cfg.silence_ms)),
        ("min_speech_ms", Json::Num(cfg.min_speech_ms)),
    ]);
    atomic_write_bytes(
        Path::new(CONFIG_FILE),
        j.to_string_pretty().as_bytes(),
        0o600,
        true,
    )
    .map_err(|e| e.to_string())
}

fn write_status(state: &str, last_asr: &str, last_answer: &str, error: &str) {
    let j = obj(vec![
        ("state", Json::Str(state.into())),
        ("last_asr", Json::Str(last_asr.into())),
        ("last_answer", Json::Str(last_answer.into())),
        ("error", Json::Str(error.into())),
        ("updated_at", Json::Num(now_unix_i64() as f64)),
    ]);
    let _ = atomic_write_bytes(
        Path::new(STATUS_FILE),
        j.to_string_compact().as_bytes(),
        0o644,
        false,
    );
}

fn is_mic_muted() -> bool {
    fs::read_to_string(BUTTONS_FILE)
        .ok()
        .and_then(|s| Json::parse(&s).ok())
        .and_then(|j| j.get("mic_muted")?.as_bool())
        .unwrap_or(false)
}

fn consume_button_trigger() -> bool {
    let mut hit = BUTTON_TRIGGERED.swap(false, Ordering::SeqCst);
    if Path::new(TRIGGER_FILE).exists() && fs::remove_file(TRIGGER_FILE).is_ok() {
        hit = true;
    }
    hit
}

fn normalize_text(text: &str) -> Vec<String> {
    let mut clean = String::with_capacity(text.len());
    for ch in text.chars() {
        for lc in ch.to_lowercase() {
            let mapped = if lc == 'ё' { 'е' } else { lc };
            if mapped.is_alphanumeric() {
                clean.push(mapped);
            } else {
                clean.push(' ');
            }
        }
    }
    clean.split_whitespace().map(str::to_string).collect()
}

const ATRI_ALIASES: &[&str] = &[
    "atri", "atria", "атри", "атря", "атрия", "атре", "атрий", "атрии", "отри", "этри", "этрий",
    "адри", "антри", "артри", "актри", "а три", "о три", "эй три", "матри", "мотри", "утри",
    "атриа", "атрей", "атрик", "отрыв", "и три", "ну три",
];
const ALICE_ALIASES: &[&str] = &["алиса", "алису", "алисе", "алисой", "alice"];
const FILLERS: &[&str] = &[
    "please",
    "пожалуйста",
    "эй",
    "слушай",
    "алло",
    "давай",
    "ну",
    "окей",
    "ок",
    "hey",
    "ok",
    "okay",
];
const CANCEL_PHRASES: &[&str] = &[
    "стоп", "отмена", "хватит", "ничего", "не надо", "забудь", "молчи",
];

fn wake_candidates(wake_word: &str) -> Vec<String> {
    let ww = wake_word.trim().to_lowercase();
    if ww.is_empty() || matches!(ww.as_str(), "off" | "none" | "false") {
        return Vec::new();
    }
    let mut out: Vec<String> = ww
        .split(',')
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .collect();
    let has_atri = out.iter().any(|s| s == "atri" || s == "атри");
    let has_alice = out.iter().any(|s| s == "алиса" || s == "alice");
    if has_atri {
        for a in ATRI_ALIASES {
            if !out.iter().any(|x| x == a) {
                out.push((*a).into());
            }
        }
    }
    if has_alice {
        for a in ALICE_ALIASES {
            if !out.iter().any(|x| x == a) {
                out.push((*a).into());
            }
        }
    }
    out.sort_by(|a, b| b.len().cmp(&a.len()));
    out
}

pub fn matches_wake_word(text: &str, wake_word: &str) -> bool {
    let cands = wake_candidates(wake_word);
    if cands.is_empty() {
        return true;
    }
    let words = normalize_text(text);
    if words.is_empty() {
        return false;
    }
    let norm = words.join(" ");
    for c in &cands {
        if norm == *c {
            return true;
        }
        if !c.contains(' ') && words.iter().any(|w| w == c) {
            return true;
        }
        if c.contains(' ') {
            if let Some(pos) = norm.find(c.as_str()) {
                let left_ok = pos == 0 || norm.as_bytes()[pos - 1] == b' ';
                let end = pos + c.len();
                let right_ok = end == norm.len() || norm.as_bytes()[end] == b' ';
                if left_ok && right_ok {
                    return true;
                }
            }
        }
    }
    if cands.iter().any(|c| c == "atri" || c == "атри") {
        let mut idx = 0;
        if idx < words.len() && FILLERS.contains(&words[idx].as_str()) {
            idx += 1;
        }
        if idx < words.len() && matches!(words[idx].as_str(), "ри" | "три") {
            return true;
        }
        if words
            .iter()
            .any(|w| w.chars().count() >= 5 && w.ends_with("атри") && w != "смотри" && w != "посмотри")
        {
            return true;
        }
    }
    false
}

pub fn strip_wake_word(text: &str, wake_word: &str) -> String {
    let mut words = normalize_text(text);
    if words.is_empty() {
        return String::new();
    }
    let cands = wake_candidates(wake_word);
    if cands.is_empty() {
        return words.join(" ");
    }
    while !words.is_empty() && FILLERS.contains(&words[0].as_str()) {
        words.remove(0);
    }
    let mut s = words.join(" ");
    let mut matched = false;
    for w in &cands {
        if s == *w {
            s.clear();
            matched = true;
            break;
        }
        if let Some(rest) = s.strip_prefix(&format!("{w} ")) {
            s = rest.trim().to_string();
            matched = true;
            break;
        }
    }
    if !matched && cands.iter().any(|c| c == "atri" || c == "атри") {
        if s == "ри" || s == "три" {
            s.clear();
            matched = true;
        } else {
            for cl in ["ри ", "три "] {
                if let Some(rest) = s.strip_prefix(cl) {
                    if !rest.trim().is_empty() {
                        s = rest.trim().to_string();
                        matched = true;
                        break;
                    }
                }
            }
        }
        if !matched {
            if let Some(first) = words.first() {
                if first.chars().count() >= 5
                    && first.ends_with("атри")
                    && first != "смотри"
                    && first != "посмотри"
                {
                    s = words[1..].join(" ");
                }
            }
        }
    }
    for w in &cands {
        if let Some(rest) = s.strip_suffix(&format!(" {w}")) {
            s = rest.trim().to_string();
            break;
        }
    }
    let mut rest: Vec<&str> = s.split_whitespace().collect();
    while !rest.is_empty() && FILLERS.contains(&rest[0]) {
        rest.remove(0);
    }
    while !rest.is_empty() && FILLERS.contains(rest.last().unwrap()) {
        rest.pop();
    }
    rest.join(" ")
}

fn is_wake_word_only(text: &str, wake_word: &str) -> bool {
    let ww = wake_word.trim().to_lowercase();
    if ww.is_empty() || matches!(ww.as_str(), "off" | "none" | "false") {
        return false;
    }
    matches_wake_word(text, wake_word) && strip_wake_word(text, wake_word).is_empty()
}

fn local_command(text: &str, wake_word: &str) -> Option<&'static str> {
    if !wake_word.is_empty() && !matches_wake_word(text, wake_word) {
        return None;
    }
    let norm = strip_wake_word(text, wake_word);
    let words: Vec<&str> = norm
        .split_whitespace()
        .filter(|w| !FILLERS.contains(w))
        .collect();
    let mut dedup = Vec::with_capacity(words.len());
    for &w in &words {
        if dedup.last().copied() != Some(w) {
            dedup.push(w);
        }
    }
    let c1 = words.join(" ");
    let c2 = dedup.join(" ");
    let check = |list: &[&str]| list.iter().any(|&p| p == c1 || p == c2);

    if check(&[
        "next", "next track", "вперед", "дальше", "другой трек", "другую песню",
        "переключи вперед", "переключи песню", "переключи трек", "перемотай вперед",
        "след трек", "следующая", "следующая песня", "следующий", "следующий трек",
        "следующую песню", "трек вперед",
    ]) {
        return Some("next_track");
    }
    if check(&[
        "pause", "stop", "выключи музыку", "на паузу", "останови", "останови музыку",
        "пауза", "поставь на паузу", "стоп", "хватит играть",
    ]) {
        return Some("pause");
    }
    if check(&[
        "play", "включи музыку", "сними с паузы", "продолжай", "resume",
        "возобнови", "продолжи", "играй",
    ]) {
        return Some("play");
    }
    if check(&[
        "prev", "previous", "верни песню", "верни трек", "назад", "переключи назад",
        "пред трек", "предыдущая", "предыдущая песня", "предыдущий", "предыдущий трек",
        "предыдущую песню", "прошлый трек", "трек назад",
    ]) {
        return Some("prev_track");
    }
    if check(&[
        "quieter", "volume down", "громкость вниз", "звук тише", "потише",
        "сделай потише", "сделай тише", "тише", "убавь", "убавь громкость",
        "убавь звук", "уменьши громкость", "уменьши звук",
    ]) {
        return Some("volume_down");
    }
    if check(&[
        "louder", "volume up", "громкости прибавь", "громкость вверх", "громче",
        "звук громче", "погромче", "прибавь", "прибавь громкость", "прибавь звук",
        "сделай громче", "сделай погромче", "увеличь громкость",
    ]) {
        return Some("volume_up");
    }
    None
}

// --- Visuals ---
struct VisualState {
    phase: String,
    ring_tag: String,
    screen_tag: String,
    ring_child: Option<Child>,
    screen_child: Option<Child>,
    udp: Option<UdpSocket>,
}

static VISUAL: OnceLock<Mutex<VisualState>> = OnceLock::new();

fn visual() -> &'static Mutex<VisualState> {
    VISUAL.get_or_init(|| {
        Mutex::new(VisualState {
            phase: "idle".into(),
            ring_tag: "off".into(),
            screen_tag: "clock".into(),
            ring_child: None,
            screen_child: None,
            udp: None,
        })
    })
}

fn send_voice_level(level: Option<f64>) {
    let (payload, txt) = match level {
        None => ("level:none\n".to_string(), None),
        Some(v) => {
            let c = v.clamp(0.0, 1.0);
            (format!("level:{c:.3}\n"), Some(format!("{c:.3}")))
        }
    };
    {
        let mut v = visual().lock().unwrap();
        if v.udp.is_none() {
            v.udp = UdpSocket::bind("0.0.0.0:0").ok();
        }
        if let Some(ref s) = v.udp {
            let _ = s.send_to(payload.as_bytes(), "127.0.0.1:5055");
        }
    }
    match txt {
        None => {
            let _ = fs::remove_file(VOICE_LEVEL_FILE);
        }
        Some(t) => {
            let _ = fs::write(VOICE_LEVEL_FILE, t);
        }
    }
}

fn set_vad_visual(phase: &str, force: bool, variant: &str) {
    let ev = match variant.trim().to_ascii_lowercase().as_str() {
        "happy" => "happy",
        "sad" => "sad",
        "angry" | "serious" => "angry",
        "surprised" => "surprised",
        _ => "default",
    };
    let (ring_tag, ring_args, screen_tag, screen_args, mode_str): (
        &str,
        Vec<&str>,
        String,
        Vec<String>,
        &str,
    ) = match phase {
        "vad" => (
            "amber",
            vec!["ring", "amber", "30"],
            "eyes_listening".into(),
            vec!["screen".into(), "eyes".into(), "listening".into()],
            "ai",
        ),
        "thinking" => (
            "amber",
            vec!["ring", "amber", "30"],
            "eyes_thinking".into(),
            vec!["screen".into(), "eyes".into(), "thinking".into()],
            "ai",
        ),
        "speaking" => (
            "amber_green",
            vec!["ring", "amber_green", "60"],
            format!("eyes_speaking_{ev}"),
            vec!["screen".into(), "eyes".into(), format!("speaking:{ev}")],
            "ai",
        ),
        _ => (
            "off",
            vec!["ring", "off"],
            "clock".into(),
            vec!["screen".into(), "clock".into()],
            "clock",
        ),
    };

    let _ = fs::create_dir_all("/var/run/atrios");
    let _ = fs::write(SCREEN_MODE_FILE, mode_str);
    let _ = fs::write("/var/run/atrios/ring_mode", ring_tag);

    let mut v = visual().lock().unwrap();
    v.phase = phase.to_string();
    let volume_override = Path::new(RING_OVERRIDE_FILE).exists();
    let ring_alive = v
        .ring_child
        .as_mut()
        .map(|c| matches!(c.try_wait(), Ok(None)))
        .unwrap_or(false);

    if !volume_override && (force || ring_tag != v.ring_tag || (ring_tag != "off" && !ring_alive)) {
        if let Some(mut old) = v.ring_child.take() {
            let _ = old.kill();
            let _ = old.wait();
        }
        let _ = Command::new("pkill")
            .args(["-f", "/usr/lib/atrios/light ring"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        v.ring_child = Command::new("/usr/lib/atrios/light")
            .args(&ring_args)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .ok();
        v.ring_tag = ring_tag.to_string();
    }

    if phase == "idle" {
        if force || v.screen_tag != "clock" {
            if let Some(mut old) = v.screen_child.take() {
                let _ = old.kill();
                let _ = old.wait();
            }
            let _ = Command::new("pkill")
                .args(["-f", "/usr/lib/atrios/light screen"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            let _ = Command::new("/usr/lib/atrios/light")
                .args(&screen_args)
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn();
            v.screen_tag = "clock".into();
        }
    } else {
        let udp_msg = match phase {
            "vad" => "state:listening\n".to_string(),
            "thinking" => "state:thinking\n".to_string(),
            "speaking" => format!("speaking:{ev}\n"),
            _ => "state:idle\n".to_string(),
        };
        let screen_alive = v
            .screen_child
            .as_mut()
            .map(|c| matches!(c.try_wait(), Ok(None)))
            .unwrap_or(false);
        if screen_alive && !force {
            if v.udp.is_none() {
                v.udp = UdpSocket::bind("0.0.0.0:0").ok();
            }
            if let Some(ref s) = v.udp {
                let _ = s.send_to(udp_msg.as_bytes(), "127.0.0.1:5055");
            }
            v.screen_tag = screen_tag;
        } else {
            if let Some(mut old) = v.screen_child.take() {
                let _ = old.kill();
                let _ = old.wait();
            }
            let _ = Command::new("pkill")
                .args(["-f", "/usr/lib/atrios/light screen"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            v.screen_child = Command::new("/usr/lib/atrios/light")
                .args(&screen_args)
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .ok();
            v.screen_tag = screen_tag;
        }
    }
}

fn check_resume_visual() {
    if RESUME_VISUAL.swap(false, Ordering::SeqCst) {
        let ph = visual().lock().unwrap().phase.clone();
        if ph != "idle" {
            set_vad_visual(&ph, true, "default");
        }
    }
}

// --- WAV / Audio helpers ---
struct WavInfo {
    channels: u16,
    sample_rate: u32,
    bits_per_sample: u16,
    data_offset: usize,
    data_len: usize,
}

fn parse_wav(data: &[u8]) -> Result<WavInfo, String> {
    if data.len() < 44 || &data[0..4] != b"RIFF" || &data[8..12] != b"WAVE" {
        return Err("Invalid TTS WAV".into());
    }
    let mut pos = 12usize;
    let mut channels = 0u16;
    let mut sample_rate = 0u32;
    let mut bits = 0u16;
    let mut found_fmt = false;
    while pos + 8 <= data.len() {
        let id = &data[pos..pos + 4];
        let sz = u32::from_le_bytes(data[pos + 4..pos + 8].try_into().unwrap()) as usize;
        let body = pos + 8;
        if body + sz > data.len() {
            return Err("Truncated TTS WAV".into());
        }
        if id == b"fmt " {
            if sz < 16 {
                return Err("Invalid fmt chunk in WAV".into());
            }
            channels = u16::from_le_bytes([data[body + 2], data[body + 3]]);
            sample_rate = u32::from_le_bytes(data[body + 4..body + 8].try_into().unwrap());
            bits = u16::from_le_bytes([data[body + 14], data[body + 15]]);
            found_fmt = true;
        } else if id == b"data" && found_fmt {
            if !(channels == 1 || channels == 2) || bits != 16 || !(8000..=96000).contains(&sample_rate) {
                return Err("Unsupported TTS WAV parameters".into());
            }
            let frame_bytes = (channels as usize) * 2;
            let frames = sz / frame_bytes;
            if frames == 0 || frames > (sample_rate as usize) * 120 || sz != frames * frame_bytes {
                return Err("Truncated or invalid TTS WAV".into());
            }
            return Ok(WavInfo {
                channels,
                sample_rate,
                bits_per_sample: bits,
                data_offset: body,
                data_len: sz,
            });
        }
        pos = body + sz + (sz & 1);
    }
    Err("Missing WAV data chunk".into())
}

fn encode_wav_pcm16(pcm: &[u8], sr: u32, ch: u16) -> Vec<u8> {
    let data_len = pcm.len() as u32;
    let riff_len = 36 + data_len;
    let byte_rate = sr * (ch as u32) * 2;
    let block_align = ch * 2;
    let mut out = Vec::with_capacity(44 + pcm.len());
    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&riff_len.to_le_bytes());
    out.extend_from_slice(b"WAVEfmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes());
    out.extend_from_slice(&ch.to_le_bytes());
    out.extend_from_slice(&sr.to_le_bytes());
    out.extend_from_slice(&byte_rate.to_le_bytes());
    out.extend_from_slice(&block_align.to_le_bytes());
    out.extend_from_slice(&16u16.to_le_bytes());
    out.extend_from_slice(b"data");
    out.extend_from_slice(&data_len.to_le_bytes());
    out.extend_from_slice(pcm);
    out
}

fn wav_envelope(data: &[u8], window: f64, default_sr: u32) -> Vec<f64> {
    let (frames, ch, sr) = if data.starts_with(b"RIFF") {
        let Ok(info) = parse_wav(data) else { return Vec::new() };
        let _ = info.bits_per_sample;
        (&data[info.data_offset..info.data_offset + info.data_len], info.channels as usize, info.sample_rate)
    } else {
        (data, 1usize, default_sr)
    };
    let n_samples = frames.len() / 2;
    if n_samples == 0 || sr == 0 || ch == 0 {
        return Vec::new();
    }
    let chunk_samples = (((sr as f64) * window).round() as usize * ch).max(ch);
    let mut rms_list = Vec::new();
    for start in (0..n_samples).step_by(chunk_samples) {
        let end = (start + chunk_samples).min(n_samples);
        let mut sum = 0.0f64;
        for i in start..end {
            let s = i16::from_le_bytes([frames[i * 2], frames[i * 2 + 1]]) as f64;
            sum += s * s;
        }
        rms_list.push((sum / ((end - start) as f64)).sqrt());
    }
    let peak = rms_list.iter().copied().fold(0.0f64, f64::max);
    if peak < 30.0 {
        return vec![0.0; rms_list.len()];
    }
    let gate = peak * 0.035;
    let denom = peak * 0.75;
    rms_list
        .into_iter()
        .map(|r| if r < gate { 0.0 } else { (r / denom).min(1.0) })
        .collect()
}

fn pcm16_rms(chunk: &[u8]) -> f64 {
    let n = chunk.len() / 2;
    if n == 0 {
        return 0.0;
    }
    let mut sum = 0.0f64;
    for i in 0..n {
        sum += i16::from_le_bytes([chunk[i * 2], chunk[i * 2 + 1]]) as f64;
    }
    let mean = sum / (n as f64);
    let mut sq = 0.0f64;
    for i in 0..n {
        let d = (i16::from_le_bytes([chunk[i * 2], chunk[i * 2 + 1]]) as f64) - mean;
        sq += d * d;
    }
    (sq / (n as f64)).sqrt()
}

fn downsample_48k_8ch_s32_to_16k_s16(raw: &[u8]) -> Vec<u8> {
    let n_i32 = raw.len() / 4;
    let stride = CAP_CH * 3;
    if n_i32 < stride {
        return Vec::new();
    }
    let mut out = Vec::with_capacity((n_i32 / stride) * 2 + 2);
    let limit = n_i32 - CAP_CH + 1;
    let mut i = 0;
    while i < limit {
        let mut s: i64 = 0;
        for c in 0..8 {
            let b = (i + c) * 4;
            s += i32::from_le_bytes([raw[b], raw[b + 1], raw[b + 2], raw[b + 3]]) as i64;
        }
        let sample = (s >> 14).clamp(-32768, 32767) as i16;
        out.extend_from_slice(&sample.to_le_bytes());
        i += stride;
    }
    out
}

fn drain_capture_fd_now() {
    let fd = CAPTURE_FD.load(Ordering::SeqCst);
    if fd < 0 {
        return;
    }
    let fsz = CAPTURE_FRAME_BYTES
        .load(Ordering::SeqCst)
        .clamp(FRAME_BYTES, CAP_FRAME_BYTES);
    let mut tmp = vec![0u8; fsz];
    loop {
        let mut pfd = PollFd { fd, events: POLLIN, revents: 0 };
        let rc = unsafe { poll(&mut pfd, 1, 0) };
        if rc <= 0 || (pfd.revents & POLLIN) == 0 {
            break;
        }
        let mut got = 0usize;
        while got < fsz {
            let n = recv_or_read(fd, &mut tmp[got..fsz]);
            if n <= 0 {
                return;
            }
            got += n as usize;
        }
    }
}

fn play_audio_bytes(data: &[u8]) -> Result<bool, String> {
    if data.is_empty() {
        return Ok(true);
    }
    let is_wav = data.starts_with(b"RIFF");
    if is_wav {
        parse_wav(data)?;
    }
    let tmp_path = format!(
        "/tmp/atrios-tts-{}.{}",
        std::process::id(),
        if is_wav { "wav" } else { "pcm" }
    );
    fs::write(&tmp_path, data).map_err(|e| e.to_string())?;
    let env = wav_envelope(data, 0.04, 48000);
    let mut cmd = Command::new("aplay");
    cmd.arg("-q");
    if !is_wav {
        cmd.args(["-t", "raw", "-f", "S16_LE", "-r", "48000", "-c", "1"]);
    }
    cmd.arg(&tmp_path)
        .stdout(Stdio::null())
        .stderr(Stdio::piped());

    let res = (|| -> Result<bool, String> {
        let mut child = cmd.spawn().map_err(|e| format!("aplay spawn: {e}"))?;
        let t0 = Instant::now();
        loop {
            match child.try_wait() {
                Ok(Some(status)) => {
                    if !status.success() && status.code() != Some(15) {
                        let mut err_s = String::new();
                        if let Some(mut se) = child.stderr.take() {
                            let _ = se.read_to_string(&mut err_s);
                        }
                        let msg = if err_s.trim().is_empty() {
                            "Audio playback failed".into()
                        } else {
                            err_s.trim().to_string()
                        };
                        set_error("audio", Some("audio"), &msg, 120);
                        return Err(msg);
                    }
                    set_error("audio", None, "", 120);
                    return Ok(true);
                }
                Ok(None) => {
                    check_resume_visual();
                    drain_capture_fd_now();
                    if consume_button_trigger() {
                        println!("[atriai] funcbutton pressed during playback -> cancelled -> clock");
                        let _ = child.kill();
                        let _ = child.wait();
                        set_error("audio", None, "", 120);
                        return Ok(false);
                    }
                    if !env.is_empty() {
                        let idx = (t0.elapsed().as_secs_f64() / 0.04) as usize;
                        send_voice_level(Some(env.get(idx).copied().unwrap_or(0.0)));
                    }
                    thread::sleep(Duration::from_millis(40));
                }
                Err(e) => return Err(e.to_string()),
            }
        }
    })();
    send_voice_level(None);
    let _ = fs::remove_file(&tmp_path);
    res
}

fn tone_pcm16(freq: f64, dur_ms: usize) -> Vec<u8> {
    let sr = 48000usize;
    let n = (sr * dur_ms / 1000).max(1);
    let fade = (sr * 10 / 1000).clamp(1, (n / 4).max(1));
    let mut out = Vec::with_capacity(n * 2);
    let w = 2.0 * std::f64::consts::PI * freq / (sr as f64);
    for i in 0..n {
        let env = if i < fade {
            (i as f64) / (fade as f64)
        } else if i >= n - fade {
            ((n - 1 - i) as f64) / (fade as f64)
        } else {
            1.0
        };
        let s = (5500.0 * env * (w * (i as f64)).sin()) as i16;
        out.extend_from_slice(&s.to_le_bytes());
    }
    out
}

fn play_command_chime(action: &str) {
    let notes: &[(f64, usize)] = match action {
        "volume_up" => &[(523.25, 55), (659.25, 80)],
        "volume_down" => &[(659.25, 55), (523.25, 80)],
        "next_track" => &[(587.33, 50), (880.00, 75)],
        "prev_track" => &[(880.00, 50), (587.33, 75)],
        "pause" => &[(440.00, 85)],
        "play" => &[(440.00, 50), (554.37, 75)],
        _ => &[(587.33, 70)],
    };
    let mut pcm = Vec::new();
    let gap_bytes = (48000 * 18 / 1000) * 2;
    for (idx, &(f, ms)) in notes.iter().enumerate() {
        if idx > 0 {
            pcm.extend(std::iter::repeat(0u8).take(gap_bytes));
        }
        pcm.extend_from_slice(&tone_pcm16(f, ms));
    }
    if let Ok(mut child) = Command::new("aplay")
        .args(["-q", "-t", "raw", "-f", "S16_LE", "-r", "48000", "-c", "1"])
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    {
        if let Some(mut sin) = child.stdin.take() {
            let _ = sin.write_all(&pcm);
        }
        let _ = child.wait();
    }
    drain_capture_fd_now();
}

fn execute_device_command(action: &str) {
    let bt_cmd = match action {
        "next_track" => Some("next"),
        "prev_track" => Some("prev"),
        "pause" => Some("pause"),
        "play" => Some("play"),
        _ => None,
    };
    if let Some(c) = bt_cmd {
        if Path::new(BT_CMD_SOCK).exists() {
            if let Ok(sock) = UnixDatagram::unbound() {
                let _ = sock.send_to(c.as_bytes(), BT_CMD_SOCK);
            }
        }
    } else if action == "volume_up" || action == "volume_down" {
        let cur = fs::read_to_string(VOLUME_FILE)
            .ok()
            .and_then(|s| s.trim().parse::<i32>().ok())
            .unwrap_or(10);
        let nxt = (cur + if action == "volume_up" { 5 } else { -5 }).clamp(0, 100);
        let _ = Command::new("/usr/bin/atrios")
            .args(["volume", &nxt.to_string()])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        println!("[atriai] volume: {cur}% -> {nxt}%");
    }
    play_command_chime(action);
}

static VOICE_BUSY: AtomicBool = AtomicBool::new(false);

struct VoiceBusyGuard;
impl Drop for VoiceBusyGuard {
    fn drop(&mut self) {
        VOICE_BUSY.store(false, Ordering::SeqCst);
    }
}

fn voice_call<T: Send + 'static>(f: impl FnOnce() -> Result<T, String> + Send + 'static) -> Result<T, String> {
    if VOICE_BUSY
        .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
        .is_err()
    {
        return Err("Previous HTTPS request is still finishing; retry shortly".into());
    }
    let (tx, rx) = mpsc::channel();
    thread::spawn(move || {
        let _g = VoiceBusyGuard;
        let _ = tx.send(f());
    });
    let deadline = Instant::now() + Duration::from_secs(26);
    loop {
        if Instant::now() > deadline {
            return Err("TimeoutError: HTTPS voice request deadline exceeded".into());
        }
        if consume_button_trigger() || is_mic_muted() {
            return Err("InterruptedError: Voice request cancelled".into());
        }
        match rx.recv_timeout(Duration::from_millis(50)) {
            Ok(res) => return res,
            Err(mpsc::RecvTimeoutError::Timeout) => check_resume_visual(),
            Err(mpsc::RecvTimeoutError::Disconnected) => return Err("Voice worker failed".into()),
        }
    }
}

fn send_phrase_to_server(
    server_url: &str,
    pcm: &[u8],
    wake_word: &str,
    force_active: bool,
) -> Result<(String, String, bool), String> {
    require_https(server_url)?;
    if pcm.len() > SR * 2 * 30 || pcm.len() % 2 != 0 {
        return Err("Invalid or oversized PCM16 phrase".into());
    }
    let cfg = load_config();
    let base = server_url.trim_end_matches('/').to_string();
    let wav = encode_wav_pcm16(pcm, SR as u32, 1);
    let mut keep_listening = false;

    let res: Result<(String, String, bool), String> = (|| {
        let base1 = base.clone();
        let tok1 = cfg.token.clone();
        let ca1 = cfg.ca_file.clone();
        let raw_asr = voice_call(move || {
            let auth = format!("Bearer {tok1}");
            let mut h = vec![("Content-Type", "audio/wav"), ("Accept", "application/json")];
            if !tok1.is_empty() {
                h.push(("Authorization", &auth));
            }
            let ca = if ca1.is_empty() { None } else { Some(ca1.as_str()) };
            request_bytes(&format!("{base1}/asr"), Some(&wav), &h, ca, 25, 65536, None)
        })?;
        let rec_txt = String::from_utf8(raw_asr).map_err(|_| "Invalid UTF-8 ASR response")?;
        let rec = Json::parse(&rec_txt)?;
        let text = rec.get_str("text").ok_or("Invalid ASR response")?.trim().to_string();
        if text.is_empty() {
            set_error("voice", None, "", 120);
            if force_active {
                keep_listening = true;
                set_vad_visual("vad", false, "default");
                return Ok((text, String::new(), true));
            }
            return Ok((text, String::new(), false));
        }
        if !wake_word.is_empty() && !matches_wake_word(&text, wake_word) {
            println!("[atriai] ignored phrase without wake word ({wake_word:?}): {text:?}");
            set_error("voice", None, "", 120);
            return Ok((text, String::new(), false));
        }
        if is_wake_word_only(&text, wake_word) {
            WAKE_TRIGGERED.store(true, Ordering::SeqCst);
            keep_listening = true;
            println!("[atriai] wake word activated ({text:?}) -> listening (7s)");
            set_vad_visual("vad", false, "default");
            play_command_chime("play");
            drain_capture_fd_now();
            set_error("voice", None, "", 120);
            return Ok((text, String::new(), true));
        }
        if wake_word.is_empty() {
            let stripped = strip_wake_word(&text, "Atri");
            if CANCEL_PHRASES.contains(&stripped.as_str()) {
                println!("[atriai] follow-up cancelled by phrase ({text:?}) -> idle");
                set_error("voice", None, "", 120);
                return Ok((text, String::new(), false));
            }
        }
        if let Some(action) = local_command(&text, wake_word) {
            execute_device_command(action);
            set_error("voice", None, "", 120);
            return Ok((text, String::new(), false));
        }

        set_vad_visual("thinking", false, "default");
        let base2 = base.clone();
        let tok2 = cfg.token.clone();
        let ca2 = cfg.ca_file.clone();
        let q_text = text.clone();
        let chat = voice_call(move || {
            let auth = format!("Bearer {tok2}");
            let mut h = Vec::new();
            if !tok2.is_empty() {
                h.push(("Authorization", auth.as_str()));
            }
            let ca = if ca2.is_empty() { None } else { Some(ca2.as_str()) };
            let payload = obj(vec![("text", Json::Str(q_text))]);
            request_json(&format!("{base2}/chat"), Some(&payload), &h, ca, 25, 1024 * 1024, None)
        })?;
        let answer = chat
            .get_str("text")
            .map(str::to_string)
            .filter(|s| !s.trim().is_empty())
            .ok_or("Empty or invalid chat response")?;
        let emotion = chat.get_str("emotion").unwrap_or("neutral").to_string();

        let base3 = base;
        let tok3 = cfg.token.clone();
        let ca3 = cfg.ca_file.clone();
        let ans_c = answer.clone();
        let emo_c = emotion.clone();
        let audio = voice_call(move || {
            let auth = format!("Bearer {tok3}");
            let mut h = vec![("Content-Type", "application/json"), ("Accept", "audio/wav")];
            if !tok3.is_empty() {
                h.push(("Authorization", &auth));
            }
            let ca = if ca3.is_empty() { None } else { Some(ca3.as_str()) };
            let payload = obj(vec![
                ("text", Json::Str(ans_c)),
                ("emotion", Json::Str(emo_c)),
            ])
            .to_string_compact();
            request_bytes(
                &format!("{base3}/tts"),
                Some(payload.as_bytes()),
                &h,
                ca,
                25,
                8 * 1024 * 1024,
                None,
            )
        })?;
        parse_wav(&audio)?;
        write_status("speaking", &text, &answer, "");
        set_vad_visual("speaking", false, &emotion);
        let completed = play_audio_bytes(&audio)?;
        set_error("voice", None, "", 120);
        if completed {
            keep_listening = true;
            set_vad_visual("vad", false, "default");
            Ok((text, answer, true))
        } else {
            Ok((text, String::new(), false))
        }
    })();

    if let Err(ref e) = res {
        if !e.starts_with("InterruptedError") {
            set_error("voice", Some(error_kind_from_msg(e, "server")), e, 120);
        }
    }
    if !keep_listening {
        set_vad_visual("idle", false, "default");
    }
    if let Err(e) = &res {
        if e.starts_with("InterruptedError") {
            return Ok((String::new(), String::new(), false));
        }
    }
    res
}

fn playback_running() -> bool {
    let Ok(cards) = fs::read_dir("/proc/asound") else { return false };
    for c in cards.flatten() {
        let name = c.file_name().to_string_lossy().into_owned();
        if !name.starts_with("card") {
            continue;
        }
        if let Ok(devs) = fs::read_dir(c.path()) {
            for d in devs.flatten() {
                let dn = d.file_name().to_string_lossy().into_owned();
                if dn.starts_with("pcm") && dn.ends_with('p') {
                    if let Ok(subs) = fs::read_dir(d.path()) {
                        for s in subs.flatten() {
                            if s.file_name().to_string_lossy().starts_with("sub") {
                                if let Ok(st) = fs::read_to_string(s.path().join("status")) {
                                    if st.contains("RUNNING") {
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    false
}

fn stop_capture(child: &mut Option<Child>) {
    CAPTURE_FD.store(-1, Ordering::SeqCst);
    if let Some(mut c) = child.take() {
        unsafe { kill(c.id() as i32, 15) };
        for _ in 0..15 {
            if matches!(c.try_wait(), Ok(Some(_))) {
                return;
            }
            thread::sleep(Duration::from_millis(10));
        }
        let _ = c.kill();
        let _ = c.wait();
    }
}

fn drain_capture_pipe(child: &mut Option<Child>, settle_ms: u64, frame_bytes: usize) {
    let Some(ref mut c) = child else { return };
    if matches!(c.try_wait(), Ok(Some(_))) {
        return;
    }
    let Some(ref mut out) = c.stdout else { return };
    let fd = out.as_raw_fd();
    let mut tmp = vec![0u8; frame_bytes];
    // First flush all immediately available frames so settle_ms starts from now.
    loop {
        let mut pfd = PollFd { fd, events: POLLIN, revents: 0 };
        let rc = unsafe { poll(&mut pfd, 1, 0) };
        if rc <= 0 || (pfd.revents & POLLIN) == 0 {
            break;
        }
        let n = recv_or_read(fd, &mut tmp);
        if n <= 0 {
            return;
        }
        if (n as usize) < frame_bytes && out.read_exact(&mut tmp[n as usize..frame_bytes]).is_err() {
            return;
        }
    }
    let deadline = Instant::now() + Duration::from_millis(settle_ms);
    loop {
        let now = Instant::now();
        let rem = deadline.saturating_duration_since(now).as_millis() as i32;
        let to = if rem > 0 { rem.min(30) } else { 0 };
        let mut pfd = PollFd { fd, events: POLLIN, revents: 0 };
        let rc = unsafe { poll(&mut pfd, 1, to) };
        if rc > 0 && (pfd.revents & POLLIN) != 0 {
            let n = recv_or_read(fd, &mut tmp);
            if n <= 0 {
                break;
            }
            if (n as usize) < frame_bytes && out.read_exact(&mut tmp[n as usize..frame_bytes]).is_err() {
                break;
            }
        } else if rem <= 0 {
            break;
        }
    }
}

fn run_daemon() -> i32 {
    unsafe {
        signal(SIGUSR1, on_sigusr1 as *const () as usize);
        signal(SIGUSR2, on_sigusr2 as *const () as usize);
    }
    let _ = fs::create_dir_all("/var/run/atrios");
    let _ = fs::write(PID_FILE, format!("{}\n", std::process::id()));
    let _ = fs::remove_file(TRIGGER_FILE);
    let _ = Command::new("pkill").args(["-9", "-f", "atri-aec"]).status();
    let _ = Command::new("pkill").args(["-9", "-f", "arecord.*hw:0,3"]).status();

    let cfg0 = load_config();
    if !cfg0.enabled {
        write_status("disabled", "", "", "");
        println!("[atriai] Disabled in config; exiting.");
        return 0;
    }
    println!("[atriai] Starting voice client -> {}", cfg0.server);
    write_status("listening", "", "", "");

    let mut noise_floor = 220.0f64;
    let mut pend_force_active = false;
    let mut empty_followups: usize = 0;
    let mut proc_child: Option<Child> = None;
    let mut use_aec = false;

    loop {
        let cfg = load_config();
        if !cfg.enabled {
            stop_capture(&mut proc_child);
            write_status("disabled", "", "", "");
            break;
        }
        if is_mic_muted() {
            stop_capture(&mut proc_child);
            pend_force_active = false;
            empty_followups = 0;
            write_status("muted", "", "", "");
            thread::sleep(Duration::from_millis(500));
            continue;
        }
        let first_start = proc_child
            .as_mut()
            .map(|c| !matches!(c.try_wait(), Ok(None)))
            .unwrap_or(true);
        if first_start {
            use_aec = Path::new(AEC_BIN).exists();
            proc_child = if use_aec {
                Command::new(AEC_BIN)
                    .stdout(Stdio::piped())
                    .stderr(Stdio::piped())
                    .spawn()
                    .ok()
            } else {
                Command::new("arecord")
                    .args([
                        "-q", "-D", "hw:0,3", "-t", "raw", "-f", "S32_LE",
                        "-r", &CAP_SR.to_string(), "-c", &CAP_CH.to_string(),
                    ])
                    .stdout(Stdio::piped())
                    .stderr(Stdio::piped())
                    .spawn()
                    .ok()
            };
        }
        let Some(ref mut child) = proc_child else {
            CAPTURE_FD.store(-1, Ordering::SeqCst);
            write_status("error", "", "", "Failed starting audio capture");
            thread::sleep(Duration::from_secs(2));
            continue;
        };
        let read_bytes = if use_aec { FRAME_BYTES } else { CAP_FRAME_BYTES };
        CAPTURE_FRAME_BYTES.store(read_bytes, Ordering::SeqCst);
        let cap_fd = child.stdout.as_ref().map(|s| s.as_raw_fd()).unwrap_or(-1);
        CAPTURE_FD.store(cap_fd, Ordering::SeqCst);
        let mut raw = vec![0u8; read_bytes];
        let mut buf: Vec<u8> = Vec::new();
        let mut pre_roll: VecDeque<Vec<u8>> = VecDeque::with_capacity(20);
        let mut speaking = false;
        let mut speech_hits = 0usize;
        let mut warmup_frames = if first_start { 15usize } else { 0 };
        let mut force_active = pend_force_active;
        pend_force_active = false;
        let ww_cfg = cfg.wake_word.trim().to_string();
        let wake_required = !ww_cfg.is_empty() && !matches!(ww_cfg.to_ascii_lowercase().as_str(), "off" | "none" | "false");
        let mut wait_speech_ms = 0.0f64;
        let mut silence_ms = 0.0f64;
        let frame_ms = (FRAME_SAMPLES as f64 / SR as f64) * 1000.0;
        let vad_thr = cfg.vad_threshold;
        let max_sil = cfg.silence_ms;
        let min_bytes = (SR * 2) * (cfg.min_speech_ms as usize) / 1000;
        let mut active_thr = vad_thr;

        if force_active {
            set_vad_visual("vad", false, "default");
        }
        write_status("listening", "", "", "");

        let mut broken_stream = false;
        loop {
            check_resume_visual();
            if is_mic_muted() || !load_config().enabled {
                broken_stream = true;
                break;
            }
            if consume_button_trigger() {
                if force_active {
                    println!("[atriai] funcbutton pressed again -> cancelled -> clock");
                    speaking = false;
                    buf.clear();
                    empty_followups = 0;
                    set_vad_visual("idle", false, "default");
                    break;
                }
                force_active = true;
                empty_followups = 0;
                speaking = false;
                speech_hits = 0;
                silence_ms = 0.0;
                wait_speech_ms = 0.0;
                buf.clear();
                set_vad_visual("vad", false, "default");
                println!("[atriai] funcbutton pressed -> listening without wake word (7s timeout)");
            }
            let read_ok = child
                .stdout
                .as_mut()
                .map(|s| s.read_exact(&mut raw).is_ok())
                .unwrap_or(false);
            if !read_ok {
                broken_stream = true;
                println!("[atriai] capture stream ended, retrying...");
                break;
            }
            let chunk = if use_aec {
                raw.clone()
            } else {
                downsample_48k_8ch_s32_to_16k_s16(&raw)
            };
            let rms = pcm16_rms(&chunk);
            if warmup_frames > 0 {
                warmup_frames -= 1;
                noise_floor = (noise_floor * 0.85 + rms * 0.15).clamp(90.0, 1200.0);
                if pre_roll.len() == 20 {
                    pre_roll.pop_front();
                }
                pre_roll.push_back(chunk);
                continue;
            }
            if !speaking {
                if rms < noise_floor {
                    noise_floor = noise_floor * 0.85 + rms * 0.15;
                } else if rms < noise_floor * 1.6 {
                    noise_floor = noise_floor * 0.992 + rms * 0.008;
                }
            }
            let music_on = playback_running();
            let dyn_thr = (noise_floor * if music_on { 1.55 } else { 1.70 } + 40.0)
                .clamp(vad_thr * 0.75, 2800.0);
            active_thr = if force_active {
                dyn_thr.min((vad_thr * 0.65).max(200.0))
            } else {
                dyn_thr
            };
            let needed_hits = 2;
            if rms >= active_thr {
                speech_hits += 1;
                if !speaking && speech_hits >= needed_hits {
                    speaking = true;
                    for p in pre_roll.drain(..) {
                        buf.extend_from_slice(&p);
                    }
                    if force_active || !wake_required {
                        set_vad_visual("vad", false, "default");
                    }
                }
                if speaking {
                    silence_ms = 0.0;
                    buf.extend_from_slice(&chunk);
                    if buf.len() >= SR * 2 * 12 {
                        break;
                    }
                } else {
                    if pre_roll.len() == 20 {
                        pre_roll.pop_front();
                    }
                    pre_roll.push_back(chunk);
                }
            } else if speaking {
                speech_hits = 0;
                buf.extend_from_slice(&chunk);
                silence_ms += frame_ms;
                if silence_ms >= max_sil {
                    if buf.len() >= min_bytes {
                        break;
                    }
                    speaking = false;
                    buf.clear();
                    silence_ms = 0.0;
                    speech_hits = 0;
                }
            } else {
                speech_hits = speech_hits.saturating_sub(1);
                if pre_roll.len() == 20 {
                    pre_roll.pop_front();
                }
                pre_roll.push_back(chunk);
                if force_active {
                    wait_speech_ms += frame_ms;
                    if wait_speech_ms >= 7000.0 {
                        println!("[atriai] active listening timeout (7s without speech) -> clock");
                        break;
                    }
                }
            }
        }
        if broken_stream {
            stop_capture(&mut proc_child);
            thread::sleep(Duration::from_secs(1));
            continue;
        }
        if speaking && buf.len() >= min_bytes && !is_mic_muted() {
            write_status("thinking", "", "", "");
            if force_active || !wake_required {
                set_vad_visual("thinking", false, "default");
            }
            let ww = if force_active { "" } else { ww_cfg.as_str() };
            match send_phrase_to_server(&cfg.server, &buf, ww, force_active) {
                Ok((asr, ans, stay_active)) => {
                    drain_capture_pipe(&mut proc_child, if stay_active { 260 } else { 40 }, read_bytes);
                    if WAKE_TRIGGERED.swap(false, Ordering::SeqCst) || !ans.trim().is_empty() {
                        pend_force_active = true;
                        empty_followups = 0;
                    } else if stay_active && force_active && asr.is_empty() && empty_followups < 1 {
                        pend_force_active = true;
                        empty_followups += 1;
                    } else if force_active && !stay_active {
                        set_vad_visual("idle", false, "default");
                        empty_followups = 0;
                    } else if !pend_force_active && force_active {
                        set_vad_visual("idle", false, "default");
                        empty_followups = 0;
                    }
                    write_status("listening", &asr, &ans, "");
                }
                Err(e) => {
                    write_status("error", "", "", &e);
                    set_vad_visual("idle", false, "default");
                    empty_followups = 0;
                    eprintln!("[atriai] server error: {e}");
                    thread::sleep(Duration::from_secs(2));
                }
            }
        } else if force_active {
            set_vad_visual("idle", false, "default");
            empty_followups = 0;
        }
        let _ = active_thr;
    }
    stop_capture(&mut proc_child);
    0
}

pub fn run(args: &[String]) -> i32 {
    let cmd = args.first().map(String::as_str).unwrap_or("status");
    match cmd {
        "daemon" => run_daemon(),
        "enable" => {
            let mut cfg = load_config();
            if let Err(e) = require_https(&cfg.server) {
                eprintln!("{e}");
                return 1;
            }
            cfg.enabled = true;
            if let Err(e) = save_config(&cfg) {
                eprintln!("{e}");
                return 1;
            }
            println!("AtriAI enabled.");
            0
        }
        "disable" => {
            let mut cfg = load_config();
            cfg.enabled = false;
            let _ = save_config(&cfg);
            write_status("disabled", "", "", "");
            println!("AtriAI disabled.");
            0
        }
        "server" if args.len() > 1 => {
            let mut cfg = load_config();
            if let Err(e) = require_https(&args[1]) {
                eprintln!("{e}");
                return 1;
            }
            cfg.server = args[1].trim_end_matches('/').to_string();
            let _ = save_config(&cfg);
            println!("AtriAI server set to {}", cfg.server);
            0
        }
        "token" if args.len() > 1 => {
            let mut cfg = load_config();
            cfg.token = args[1].clone();
            let _ = save_config(&cfg);
            println!("AtriAI token updated.");
            0
        }
        "ca" if args.len() > 1 => {
            let mut cfg = load_config();
            cfg.ca_file = args[1].clone();
            let _ = save_config(&cfg);
            println!("AtriAI CA file set to {}", cfg.ca_file);
            0
        }
        "wake" if args.len() > 1 => {
            let mut cfg = load_config();
            let ww = args[1].trim();
            cfg.wake_word = if matches!(ww.to_ascii_lowercase().as_str(), "off" | "none" | "false") {
                String::new()
            } else {
                ww.to_string()
            };
            let _ = save_config(&cfg);
            println!(
                "AtriAI wake_word set to {}",
                if cfg.wake_word.is_empty() { "(off)" } else { &cfg.wake_word }
            );
            0
        }
        "ask" if args.len() > 1 => {
            let cfg = load_config();
            let Ok(mut raw) = fs::read(&args[1]) else {
                eprintln!("Cannot read {}", args[1]);
                return 1;
            };
            if raw.starts_with(b"RIFF") && raw.len() > 44 {
                match parse_wav(&raw) {
                    Ok(info) => {
                        if info.channels != 1 || info.bits_per_sample != 16 || info.sample_rate != SR as u32 {
                            eprintln!("WAV input must be 16 kHz mono PCM16");
                            return 1;
                        }
                        raw = raw[info.data_offset..info.data_offset + info.data_len].to_vec();
                    }
                    Err(e) => {
                        eprintln!("{e}");
                        return 1;
                    }
                }
            }
            match send_phrase_to_server(&cfg.server, &raw, &cfg.wake_word, false) {
                Ok((asr, ans, _)) => {
                    println!(
                        "{}",
                        obj(vec![("asr", Json::Str(asr)), ("answer", Json::Str(ans))]).to_string_compact()
                    );
                    0
                }
                Err(e) => {
                    eprintln!("{e}");
                    1
                }
            }
        }
        _ => {
            let cfg = load_config();
            let st = fs::read_to_string(STATUS_FILE)
                .ok()
                .and_then(|s| Json::parse(&s).ok())
                .unwrap_or(Json::Obj(BTreeMap::new()));
            let pub_cfg = obj(vec![
                ("enabled", Json::Bool(cfg.enabled)),
                ("server", Json::Str(cfg.server)),
                ("ca_file", Json::Str(cfg.ca_file)),
                ("token", Json::Str(if cfg.token.is_empty() { String::new() } else { "***".into() })),
                ("wake_word", Json::Str(cfg.wake_word)),
                ("vad_threshold", Json::Num(cfg.vad_threshold)),
                ("silence_ms", Json::Num(cfg.silence_ms)),
                ("min_speech_ms", Json::Num(cfg.min_speech_ms)),
            ]);
            println!("{}", obj(vec![("config", pub_cfg), ("status", st)]).to_string_pretty());
            0
        }
    }
}
