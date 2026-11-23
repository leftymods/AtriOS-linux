use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::io::{Read, Write};
use std::net::UdpSocket;
use std::os::raw::c_int;
use std::os::unix::io::AsRawFd;
use std::os::unix::net::UnixDatagram;
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicI64, AtomicUsize, Ordering};
use std::sync::{mpsc, Mutex, OnceLock};
use std::thread;
use std::time::{Duration, Instant};

use crate::https::{migrate_legacy_url, request_bytes, request_json, request_stream, require_https};
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
            server: "https://ai.leftymods.tech".into(),
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
    let srv_lc = cfg.server.trim().to_ascii_lowercase();
    if srv_lc.starts_with("http://")
        || srv_lc.contains("://192.168.")
        || srv_lc.contains("://10.")
        || srv_lc.contains("://127.")
        || srv_lc.contains("://localhost")
    {
        eprintln!("[atriai] direct LAN/HTTP server rejected ({}); forcing encrypted gateway https://ai.leftymods.tech", cfg.server);
        cfg.server = "https://ai.leftymods.tech".into();
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
    "атри", "atri", "atria",
    "арти", "атриум", "натрий", "астры",
    "атря", "атрия", "атре", "атрий", "атрии",
    "отри", "этри", "этрий",
    "адри", "антри", "артри", "актри",
    "а три", "эй три", "а тры", "а дри", "а тре",
    "атриа", "атрей", "атрик", "одри", "эдри", "атру", "атро",
];
const ATRI_EXCLUSIONS: &[&str] = &[
    "смотри", "посмотри", "присмотри", "осмотри", "пересмотри", "досмотри",
    "рассмотри", "подсмотри", "усмотри", "внутри", "изнутри", "утром", "утра",
    "утру", "хитри", "бодри", "мудри", "пудри",
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
const CANCEL_FILLERS: &[&str] = &[
    "все", "ладно", "ну", "да", "эй", "ой", "уже", "пожалуйста", "просто", "быстро", "сейчас",
    "же", "ты", "ок", "окей", "а", "и",
];

fn is_fuzzy_atri_token(w: &str) -> bool {
    if ATRI_EXCLUSIONS.contains(&w) {
        return false;
    }
    let chars: Vec<char> = w.chars().collect();
    if (4..=5).contains(&chars.len()) {
        let first = chars[0];
        let last = *chars.last().unwrap();
        if matches!(first, 'а' | 'э')
            && matches!(last, 'и' | 'е' | 'я' | 'й')
        {
            let rest: String = chars[1..].iter().collect();
            if rest.starts_with("тр") || rest.starts_with("др") {
                return true;
            }
        }
    }
    false
}

fn is_fuzzy_atri_pair(w1: &str, w2: &str) -> bool {
    matches!(w1, "а" | "э" | "эй") && matches!(w2, "три" | "тре" | "тры" | "дри")
}

pub fn is_cancel_token(w: &str) -> bool {
    if matches!(
        w,
        "стоп" | "stop" | "отмена" | "хватит" | "ничего" | "забудь" | "молчи" | "молчать"
            | "замолчи" | "заткнись" | "завались" | "заглохни" | "умолкни" | "закройся"
            | "стой" | "прекрати" | "достаточно" | "тихо" | "выключись" | "отключись"
            | "отстань" | "перестань" | "стопе" | "стопэ" | "стоб" | "ша" | "отбой" | "уйди"
    ) {
        return true;
    }
    if w.chars().count() >= 4
        && (w.starts_with("стоп")
            || w.ends_with("стоп")
            || w.starts_with("замолч")
            || w.starts_with("заткн")
            || w.starts_with("завал")
            || w.starts_with("заглох")
            || w.starts_with("умолк")
            || w.starts_with("закройс")
            || w.starts_with("молч")
            || w.starts_with("прекрат")
            || w.starts_with("отмен")
            || w.starts_with("отключ")
            || w.starts_with("выключ")
            || w.starts_with("отстан")
            || w.starts_with("перестан")
            || w.starts_with("хват"))
    {
        return true;
    }
    false
}

pub fn is_cancel_phrase(text: &str) -> bool {
    let words = normalize_text(text);
    if words.is_empty() {
        return false;
    }
    let mut start = 0usize;
    let mut end = words.len();
    while start < end && CANCEL_FILLERS.contains(&words[start].as_str()) {
        start += 1;
    }
    while end > start && CANCEL_FILLERS.contains(&words[end - 1].as_str()) {
        end -= 1;
    }
    let core = &words[start..end];
    if core.is_empty() {
        return false;
    }
    let joined = core.join(" ");
    if matches!(joined.as_str(), "не надо" | "не слушай" | "с топ") {
        return true;
    }
    let mut i = 0usize;
    let mut has_cancel = false;
    while i < core.len() {
        if i + 1 < core.len()
            && matches!(
                (core[i].as_str(), core[i + 1].as_str()),
                ("не", "надо") | ("не", "слушай") | ("с", "топ")
            )
        {
            has_cancel = true;
            i += 2;
            continue;
        }
        let w = core[i].as_str();
        if is_cancel_token(w) {
            has_cancel = true;
        } else if !CANCEL_FILLERS.contains(&w) {
            return false;
        }
        i += 1;
    }
    has_cancel
}

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
    let mut idx = 0usize;
    while idx < words.len() && FILLERS.contains(&words[idx].as_str()) {
        idx += 1;
    }
    if idx >= words.len() {
        return false;
    }
    let lead = words[idx..].join(" ");
    let norm = words.join(" ");
    for c in &cands {
        if lead == *c || lead.starts_with(&format!("{c} ")) {
            return true;
        }
        // Allow trailing wake word on short commands only (e.g. "громче атри")
        if words.len() <= 5 && norm.ends_with(&format!(" {c}")) {
            return true;
        }
    }
    if cands.iter().any(|c| c == "atri" || c == "атри") {
        if is_fuzzy_atri_token(&words[idx]) {
            return true;
        }
        if idx + 1 < words.len() && is_fuzzy_atri_pair(&words[idx], &words[idx + 1]) {
            return true;
        }
    }
    false
}

pub fn strip_wake_word(text: &str, wake_word: &str) -> String {
    let words = normalize_text(text);
    if words.is_empty() {
        return String::new();
    }
    let cands = wake_candidates(wake_word);
    if cands.is_empty() {
        return words.join(" ");
    }
    let has_atri = cands.iter().any(|c| c == "atri" || c == "атри");
    let mut s = words.join(" ");
    loop {
        let mut toks: Vec<String> = s.split_whitespace().map(str::to_string).collect();
        while !toks.is_empty() && FILLERS.contains(&toks[0].as_str()) {
            toks.remove(0);
        }
        s = toks.join(" ");
        if s.is_empty() {
            break;
        }
        let mut progressed = false;
        for w in &cands {
            if s == *w {
                s.clear();
                progressed = true;
                break;
            }
            if let Some(rest) = s.strip_prefix(&format!("{w} ")) {
                s = rest.trim().to_string();
                progressed = true;
                break;
            }
        }
        if !progressed && has_atri {
            if toks.len() >= 2 && is_fuzzy_atri_pair(&toks[0], &toks[1]) {
                s = toks[2..].join(" ");
                progressed = true;
            } else if let Some(first) = toks.first() {
                if is_fuzzy_atri_token(first) {
                    s = toks[1..].join(" ");
                    progressed = true;
                }
            }
        }
        if !progressed {
            break;
        }
    }
    let mut stripped_suffix = false;
    for w in &cands {
        if let Some(rest) = s.strip_suffix(&format!(" {w}")) {
            s = rest.trim().to_string();
            stripped_suffix = true;
            break;
        }
    }
    if !stripped_suffix && has_atri {
        let mut end_toks: Vec<&str> = s.split_whitespace().collect();
        if let Some(&last) = end_toks.last() {
            if is_fuzzy_atri_token(last) {
                end_toks.pop();
                s = end_toks.join(" ");
            }
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeviceCommand {
    NextTrack,
    PrevTrack,
    Pause,
    Play,
    VolumeUp(i32),
    VolumeDown(i32),
    VolumeSet(i32),
    BluetoothOn,
    BluetoothOff,
}

fn parse_russian_number(words: &[&str]) -> Option<i32> {
    for &w in words {
        if let Ok(v) = w.parse::<i32>() {
            if (0..=100).contains(&v) {
                return Some(v);
            }
        }
    }
    for &w in words {
        match w {
            "минимум" | "минимума" => return Some(5),
            "половину" | "половина" | "среднюю" | "среднее" | "средняя" => return Some(50),
            "максимум" | "максимальную" | "полную" | "максимально" => return Some(100),
            "ноль" | "нуль" => return Some(0),
            _ => {}
        }
    }
    let word_val = |w: &str| -> Option<(i32, bool)> {
        match w {
            "десять" => Some((10, false)),
            "двадцать" => Some((20, true)),
            "тридцать" => Some((30, true)),
            "сорок" => Some((40, true)),
            "пятьдесят" => Some((50, true)),
            "шестьдесят" => Some((60, true)),
            "семьдесят" => Some((70, true)),
            "восемьдесят" => Some((80, true)),
            "девяносто" => Some((90, true)),
            "сто" => Some((100, false)),
            "один" | "одну" => Some((1, false)),
            "два" | "две" => Some((2, false)),
            "три" => Some((3, false)),
            "четыре" => Some((4, false)),
            "пять" => Some((5, false)),
            "шесть" => Some((6, false)),
            "семь" => Some((7, false)),
            "восемь" => Some((8, false)),
            "девять" => Some((9, false)),
            "одиннадцать" => Some((11, false)),
            "двенадцать" => Some((12, false)),
            "тринадцать" => Some((13, false)),
            "четырнадцать" => Some((14, false)),
            "пятнадцать" => Some((15, false)),
            "шестнадцать" => Some((16, false)),
            "семнадцать" => Some((17, false)),
            "восемнадцать" => Some((18, false)),
            "девятнадцать" => Some((19, false)),
            _ => None,
        }
    };
    let mut i = 0usize;
    while i < words.len() {
        if let Some((v, is_ten)) = word_val(words[i]) {
            if is_ten && i + 1 < words.len() {
                if let Some((u, false)) = word_val(words[i + 1]) {
                    if (1..=9).contains(&u) {
                        return Some((v + u).clamp(0, 100));
                    }
                }
            }
            return Some(v.clamp(0, 100));
        }
        i += 1;
    }
    None
}

pub fn split_trailing_stop(text: &str, wake_word: &str) -> (String, bool) {
    let stripped = strip_wake_word(text, wake_word);
    let mut toks: Vec<&str> = stripped.split_whitespace().collect();
    while !toks.is_empty() && FILLERS.contains(toks.last().unwrap()) {
        toks.pop();
    }
    if toks.len() >= 2 && is_cancel_token(toks.last().unwrap()) {
        toks.pop();
        while !toks.is_empty() && FILLERS.contains(toks.last().unwrap()) {
            toks.pop();
        }
        let only_cancel_or_filler = toks
            .iter()
            .all(|&w| CANCEL_FILLERS.contains(&w) || is_cancel_token(w));
        if !toks.is_empty() && !only_cancel_or_filler {
            return (toks.join(" "), true);
        }
    }
    (stripped, false)
}

pub fn local_command(text: &str, wake_word: &str) -> Option<DeviceCommand> {
    if !wake_word.is_empty() && !matches_wake_word(text, wake_word) {
        return None;
    }
    let (norm, _) = split_trailing_stop(text, wake_word);
    let words: Vec<&str> = norm
        .split_whitespace()
        .filter(|w| !FILLERS.contains(w))
        .collect();
    if words.is_empty() {
        return None;
    }

    let has_bt = words.iter().any(|&w| {
        matches!(w, "бт" | "bt")
            || w.starts_with("блюту")
            || w.starts_with("блют")
            || w.starts_with("bluet")
    });
    if has_bt {
        if words.iter().any(|&w| {
            matches!(
                w,
                "выключи"
                    | "выключить"
                    | "отключи"
                    | "отключить"
                    | "останови"
                    | "убери"
                    | "закрой"
                    | "скрой"
                    | "выруби"
                    | "отруби"
                    | "off"
                    | "disable"
                    | "stop"
            )
        }) {
            return Some(DeviceCommand::BluetoothOff);
        }
        if words.iter().any(|&w| {
            matches!(
                w,
                "включи"
                    | "включить"
                    | "запусти"
                    | "активируй"
                    | "открой"
                    | "подключи"
                    | "режим"
                    | "сопряжение"
                    | "поиск"
                    | "вруби"
                    | "колонка"
                    | "колонки"
                    | "on"
                    | "enable"
                    | "start"
                    | "pair"
            )
        }) || words.len() <= 2
        {
            return Some(DeviceCommand::BluetoothOn);
        }
    }

    let mut dedup = Vec::with_capacity(words.len());
    for &w in &words {
        if dedup.last().copied() != Some(w) {
            dedup.push(w);
        }
    }
    let c1 = words.join(" ");
    let c2 = dedup.join(" ");
    let check = |list: &[&str]| list.iter().any(|&p| p == c1 || p == c2);

    if check(&["выключи звук", "без звука", "звук выключи", "mute"]) {
        return Some(DeviceCommand::VolumeSet(0));
    }

    if words
        .iter()
        .any(|&w| matches!(w, "громче" | "погромче" | "прибавь" | "увеличь" | "louder"))
    {
        if let Some(n) = parse_russian_number(&words) {
            if words.contains(&"на") {
                return Some(DeviceCommand::VolumeUp(n.clamp(1, 100)));
            }
            return Some(DeviceCommand::VolumeSet(n.clamp(0, 100)));
        }
        if words.iter().any(|&w| matches!(w, "чуть" | "немного" | "слегка")) {
            return Some(DeviceCommand::VolumeUp(5));
        }
        if words.iter().any(|&w| matches!(w, "сильно" | "намного" | "максимально")) {
            return Some(DeviceCommand::VolumeUp(15));
        }
        return Some(DeviceCommand::VolumeUp(10));
    }

    if words
        .iter()
        .any(|&w| matches!(w, "тише" | "потише" | "убавь" | "уменьши" | "quieter"))
    {
        if let Some(n) = parse_russian_number(&words) {
            if words.contains(&"на") {
                return Some(DeviceCommand::VolumeDown(n.clamp(1, 100)));
            }
            return Some(DeviceCommand::VolumeSet(n.clamp(0, 100)));
        }
        if words.iter().any(|&w| matches!(w, "чуть" | "немного" | "слегка")) {
            return Some(DeviceCommand::VolumeDown(5));
        }
        if words.iter().any(|&w| matches!(w, "сильно" | "намного")) {
            return Some(DeviceCommand::VolumeDown(15));
        }
        return Some(DeviceCommand::VolumeDown(10));
    }

    if words
        .iter()
        .any(|&w| matches!(w, "громкость" | "звук" | "volume"))
    {
        if let Some(n) = parse_russian_number(&words) {
            return Some(DeviceCommand::VolumeSet(n.clamp(0, 100)));
        }
        if words.contains(&"вверх") || words.contains(&"up") {
            return Some(DeviceCommand::VolumeUp(10));
        }
        if words.contains(&"вниз") || words.contains(&"down") {
            return Some(DeviceCommand::VolumeDown(10));
        }
    }

    if check(&[
        "next", "next track", "вперед", "дальше", "другой трек", "другую песню",
        "переключи вперед", "переключи песню", "переключи трек", "перемотай вперед",
        "след трек", "следующая", "следующая песня", "следующий", "следующий трек",
        "следующую песню", "трек вперед",
    ]) {
        return Some(DeviceCommand::NextTrack);
    }
    if check(&[
        "pause", "stop", "выключи музыку", "на паузу", "останови", "останови музыку",
        "пауза", "поставь на паузу", "стоп", "хватит играть",
    ]) {
        return Some(DeviceCommand::Pause);
    }
    if check(&[
        "play", "включи музыку", "сними с паузы", "продолжай", "resume",
        "возобнови", "продолжи", "играй",
    ]) {
        return Some(DeviceCommand::Play);
    }
    if check(&[
        "prev", "previous", "верни песню", "верни трек", "назад", "переключи назад",
        "пред трек", "предыдущая", "предыдущая песня", "предыдущий", "предыдущий трек",
        "предыдущую песню", "прошлый трек", "трек назад",
    ]) {
        return Some(DeviceCommand::PrevTrack);
    }
    None
}

pub fn is_direct_bluetooth_command(text: &str) -> Option<DeviceCommand> {
    let words = normalize_text(text);
    if words.len() < 2 {
        return None;
    }
    let has_bt = words
        .iter()
        .any(|w| matches!(w.as_str(), "блютуз" | "блютус" | "блютуф" | "bluetooth"));
    if !has_bt {
        return None;
    }
    if words.iter().any(|w| {
        matches!(
            w.as_str(),
            "выключи" | "выключить" | "отключи" | "отключить" | "выруби" | "убери" | "закрой" | "стоп" | "off"
        )
    }) {
        return Some(DeviceCommand::BluetoothOff);
    }
    if words.iter().any(|w| {
        matches!(
            w.as_str(),
            "включи" | "включить" | "вруби" | "запусти" | "активируй" | "открой" | "подключи" | "сопряжение" | "поиск" | "on"
        )
    }) {
        return Some(DeviceCommand::BluetoothOn);
    }
    None
}

static REMOTE_DOWN_UNTIL: AtomicI64 = AtomicI64::new(0);

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

pub fn set_vad_visual(phase: &str, force: bool, variant: &str) {
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
        "bt_on" => (
            "blue",
            vec!["ring", "blue"],
            "eyes_bt_on".into(),
            vec!["screen".into(), "eyes".into(), "bt_on".into()],
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
            "bt_on" => "state:bt_on\n".to_string(),
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

fn read_capture_frames_now() -> Vec<Vec<u8>> {
    let mut frames = Vec::new();
    let fd = CAPTURE_FD.load(Ordering::SeqCst);
    if fd < 0 {
        return frames;
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
                return frames;
            }
            got += n as usize;
        }
        if fsz == FRAME_BYTES {
            frames.push(tmp.clone());
        } else {
            frames.push(downsample_48k_8ch_s32_to_16k_s16(&tmp));
        }
        if frames.len() >= 64 {
            break;
        }
    }
    frames
}

pub fn drain_capture_fd_now() {
    let _ = read_capture_frames_now();
}

pub fn is_barge_in_stop(asr_text: &str, tts_answer: &str) -> bool {
    let asr_words = normalize_text(asr_text);
    if asr_words.is_empty() {
        return false;
    }
    if is_cancel_phrase(asr_text) {
        return true;
    }
    let ans_words = normalize_text(tts_answer);
    for w in &asr_words {
        if is_cancel_token(w) && (!ans_words.contains(w) || asr_words.len() <= 3) {
            return true;
        }
    }
    for pair in asr_words.windows(2) {
        if pair[0] == "с" && pair[1] == "топ" {
            return true;
        }
    }
    false
}

fn play_audio_bytes(
    data: &[u8],
    tts_answer: &str,
    server_base: &str,
    token: &str,
    ca_file: &str,
) -> Result<bool, String> {
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

    let s_base = server_base.to_string();
    let s_tok = token.to_string();
    let s_ca = ca_file.to_string();
    let s_ans = tts_answer.to_string();
    let spawn_barge_asr = move |pcm: Vec<u8>| -> mpsc::Receiver<bool> {
        let (tx, rx) = mpsc::channel();
        let b = s_base.clone();
        let tok = s_tok.clone();
        let ca = s_ca.clone();
        let ans = s_ans.clone();
        thread::spawn(move || {
            let hit = if crate::local_stt::is_available() {
                match crate::local_stt::transcribe_pcm16(&pcm) {
                    Ok(txt) => {
                        let t = txt.trim();
                        if !t.is_empty() {
                            println!("[tts-barge-local] asr={t:?}");
                        }
                        is_barge_in_stop(t, &ans)
                    }
                    Err(_) => false,
                }
            } else {
                let wav = prepare_asr_wav(&pcm);
                let auth = format!("Bearer {tok}");
                let mut h = vec![("Content-Type", "audio/wav"), ("Accept", "application/json")];
                if !tok.is_empty() {
                    h.push(("Authorization", &auth));
                }
                let ca_opt = if ca.is_empty() { None } else { Some(ca.as_str()) };
                match request_bytes(&format!("{b}/asr"), Some(&wav), &h, ca_opt, 4, 65536, None) {
                    Ok(raw) => {
                        let txt = String::from_utf8(raw)
                            .ok()
                            .and_then(|s| Json::parse(&s).ok())
                            .and_then(|j| j.get_str("text").map(str::to_string))
                            .unwrap_or_default();
                        let t = txt.trim();
                        if !t.is_empty() {
                            println!("[tts-barge] asr={t:?}");
                        }
                        is_barge_in_stop(t, &ans)
                    }
                    Err(_) => false,
                }
            };
            let _ = tx.send(hit);
        });
        rx
    };

    let res = (|| -> Result<bool, String> {
        let mut child = cmd.spawn().map_err(|e| format!("aplay spawn: {e}"))?;
        let t0 = Instant::now();
        let mut bg_rms = 280.0f64;
        let mut pre_roll: VecDeque<Vec<u8>> = VecDeque::with_capacity(8);
        let mut barge_buf: Vec<u8> = Vec::new();
        let mut in_barge = false;
        let mut barge_hits = 0usize;
        let mut barge_silence_ms = 0.0f64;
        let mut barge_peak = 0.0f64;
        let mut barge_rms_sum = 0.0f64;
        let mut barge_frames = 0usize;
        let mut barge_rx: Option<mpsc::Receiver<bool>> = None;
        let mut pending_barge: Option<(Vec<u8>, f64)> = None;
        let mut last_barge_rms = 280.0f64;

        loop {
            match child.try_wait() {
                Ok(Some(status)) => {
                    if in_barge && barge_buf.len() >= (SR * 2) * 200 / 1000 && !server_base.is_empty() {
                        let avg_rms = barge_rms_sum / (barge_frames.max(1) as f64);
                        let seg = std::mem::take(&mut barge_buf);
                        if barge_rx.is_none() {
                            barge_rx = Some(spawn_barge_asr(seg));
                        } else {
                            pending_barge = Some((seg, avg_rms));
                        }
                    }
                    if let Some(rx) = barge_rx.take() {
                        if let Ok(true) = rx.recv_timeout(Duration::from_millis(650)) {
                            println!("[atriai] voice barge-in STOP at end of playback -> clock");
                            set_error("audio", None, "", 120);
                            return Ok(false);
                        }
                    }
                    if let Some((pcm, _)) = pending_barge.take() {
                        if let Ok(true) = spawn_barge_asr(pcm).recv_timeout(Duration::from_millis(650)) {
                            println!("[atriai] voice barge-in STOP queued at end of playback -> clock");
                            set_error("audio", None, "", 120);
                            return Ok(false);
                        }
                    }
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
                    if let Some(ref rx) = barge_rx {
                        match rx.try_recv() {
                            Ok(true) => {
                                println!("[atriai] voice barge-in STOP detected during playback -> silenced -> clock");
                                let _ = child.kill();
                                let _ = child.wait();
                                set_error("audio", None, "", 120);
                                return Ok(false);
                            }
                            Ok(false) => {
                                bg_rms = (bg_rms * 0.65 + last_barge_rms * 0.35).clamp(180.0, 1600.0);
                                barge_rx = None;
                                if let Some((pcm, avg_rms)) = pending_barge.take() {
                                    last_barge_rms = avg_rms;
                                    barge_rx = Some(spawn_barge_asr(pcm));
                                }
                            }
                            Err(mpsc::TryRecvError::Disconnected) => {
                                barge_rx = None;
                                if let Some((pcm, avg_rms)) = pending_barge.take() {
                                    last_barge_rms = avg_rms;
                                    barge_rx = Some(spawn_barge_asr(pcm));
                                }
                            }
                            Err(mpsc::TryRecvError::Empty) => {}
                        }
                    }
                    let tts_idx = (t0.elapsed().as_secs_f64() / 0.04) as usize;
                    let tts_lvl = env.get(tts_idx).copied().unwrap_or(0.0);
                    let armed = t0.elapsed() >= Duration::from_millis(160);
                    for chunk in read_capture_frames_now() {
                        let rms = pcm16_rms(&chunk);
                        if !armed {
                            bg_rms = (bg_rms * 0.90 + rms * 0.10).clamp(180.0, 1600.0);
                            if pre_roll.len() == 8 {
                                pre_roll.pop_front();
                            }
                            pre_roll.push_back(chunk);
                            continue;
                        }
                        let start_thr = (bg_rms * 1.65 + tts_lvl * 380.0 + 180.0).clamp(580.0, 2600.0);
                        let keep_thr = (bg_rms * 1.25 + 60.0).clamp(240.0, start_thr * 0.80);
                        if !in_barge {
                            if rms >= start_thr {
                                barge_hits += 1;
                                if barge_hits >= 2 {
                                    in_barge = true;
                                    barge_buf.clear();
                                    barge_peak = rms;
                                    barge_rms_sum = 0.0;
                                    barge_frames = 0;
                                    for p in pre_roll.drain(..) {
                                        barge_buf.extend_from_slice(&p);
                                    }
                                    barge_buf.extend_from_slice(&chunk);
                                    barge_rms_sum += rms;
                                    barge_frames += 1;
                                    barge_silence_ms = 0.0;
                                    continue;
                                }
                            } else {
                                bg_rms = (bg_rms * 0.94 + rms * 0.06).clamp(180.0, 1600.0);
                                barge_hits = barge_hits.saturating_sub(1);
                            }
                            if pre_roll.len() == 8 {
                                pre_roll.pop_front();
                            }
                            pre_roll.push_back(chunk);
                        } else {
                            barge_buf.extend_from_slice(&chunk);
                            if rms > barge_peak {
                                barge_peak = rms;
                            }
                            barge_rms_sum += rms;
                            barge_frames += 1;
                            let eff_keep = keep_thr.max((barge_peak * 0.42).min(950.0));
                            if rms >= eff_keep {
                                barge_silence_ms = 0.0;
                            } else {
                                barge_silence_ms += 32.0;
                            }
                            let dur_ms = (barge_buf.len() * 1000) / (SR * 2);
                            if barge_silence_ms >= 160.0 || dur_ms >= 850 {
                                if dur_ms >= 200 && !server_base.is_empty() {
                                    let avg_rms = barge_rms_sum / (barge_frames.max(1) as f64);
                                    let seg = std::mem::take(&mut barge_buf);
                                    if barge_rx.is_none() {
                                        last_barge_rms = avg_rms;
                                        barge_rx = Some(spawn_barge_asr(seg));
                                    } else {
                                        pending_barge = Some((seg, avg_rms));
                                    }
                                } else {
                                    barge_buf.clear();
                                }
                                in_barge = false;
                                barge_hits = 0;
                                barge_silence_ms = 0.0;
                            }
                        }
                    }
                    if consume_button_trigger() {
                        println!("[atriai] funcbutton pressed during playback -> cancelled -> clock");
                        let _ = child.kill();
                        let _ = child.wait();
                        set_error("audio", None, "", 120);
                        return Ok(false);
                    }
                    if !env.is_empty() {
                        send_voice_level(Some(tts_lvl));
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

struct SynthVoice {
    start_ms: usize,
    dur_ms: usize,
    f0: f64,
    f1: f64,
    amp: f64,
    harm2: f64,
    harm3: f64,
    decay: f64,
}

fn render_synth_chime(voices: &[SynthVoice]) -> Vec<u8> {
    let sr = 48000usize;
    let total_ms = voices
        .iter()
        .map(|v| v.start_ms + v.dur_ms)
        .max()
        .unwrap_or(80)
        + 15;
    let total_samples = (sr * total_ms / 1000).max(1);
    let mut mix = vec![0.0f64; total_samples];
    for v in voices {
        let s0 = sr * v.start_ms / 1000;
        let len = (sr * v.dur_ms / 1000).max(1);
        let attack = (sr * 6 / 1000).clamp(1, (len / 4).max(1));
        let release = (sr * 18 / 1000).clamp(1, (len / 2).max(1));
        let mut phase = 0.0f64;
        for i in 0..len {
            let idx = s0 + i;
            if idx >= total_samples {
                break;
            }
            let frac = (i as f64) / (len as f64);
            let freq = v.f0 + (v.f1 - v.f0) * frac;
            phase += 2.0 * std::f64::consts::PI * freq / (sr as f64);
            let atk = if i < attack { (i as f64) / (attack as f64) } else { 1.0 };
            let rel = if i >= len - release {
                ((len - 1 - i) as f64) / (release as f64)
            } else {
                1.0
            };
            let dec = (-v.decay * frac).exp();
            let wave = phase.sin() + v.harm2 * (2.0 * phase).sin() + v.harm3 * (3.0 * phase).sin();
            mix[idx] += v.amp * atk * rel * dec * wave;
        }
    }
    let mut out = Vec::with_capacity(total_samples * 2);
    for s in mix {
        let soft = 22000.0 * (s / 22000.0).tanh();
        let v = soft.round().clamp(-32767.0, 32767.0) as i16;
        out.extend_from_slice(&v.to_le_bytes());
    }
    out
}

pub fn build_chime_pcm(action: &str, vol_pct: Option<i32>) -> Vec<u8> {
    let v_norm = vol_pct.unwrap_or(50).clamp(0, 100) as f64 / 100.0;
    let voices: Vec<SynthVoice> = match action {
        // Crystal rising two-step chime with soft upper shimmer
        "wake" => vec![
            SynthVoice { start_ms: 0, dur_ms: 95, f0: 523.25, f1: 528.0, amp: 4600.0, harm2: 0.22, harm3: 0.08, decay: 2.2 },
            SynthVoice { start_ms: 52, dur_ms: 140, f0: 783.99, f1: 783.99, amp: 4800.0, harm2: 0.18, harm3: 0.10, decay: 2.8 },
            SynthVoice { start_ms: 68, dur_ms: 150, f0: 1046.50, f1: 1046.50, amp: 2400.0, harm2: 0.10, harm3: 0.0, decay: 3.4 },
        ],
        // Pitch-scaled ascending marimba/bell pop for volume up
        "volume_up" => {
            let base = 420.0 + v_norm * 360.0;
            vec![
                SynthVoice { start_ms: 0, dur_ms: 65, f0: base, f1: base * 1.08, amp: 4800.0, harm2: 0.28, harm3: 0.06, decay: 3.6 },
                SynthVoice { start_ms: 42, dur_ms: 105, f0: base * 1.25, f1: base * 1.26, amp: 5200.0, harm2: 0.20, harm3: 0.08, decay: 3.2 },
            ]
        }
        // Pitch-scaled descending warm woodblock/bell drop for volume down
        "volume_down" => {
            let base = 340.0 + v_norm * 320.0;
            if vol_pct == Some(0) {
                vec![
                    SynthVoice { start_ms: 0, dur_ms: 90, f0: 320.0, f1: 210.0, amp: 4600.0, harm2: 0.12, harm3: 0.0, decay: 3.8 },
                    SynthVoice { start_ms: 55, dur_ms: 120, f0: 196.0, f1: 165.0, amp: 4200.0, harm2: 0.08, harm3: 0.0, decay: 4.0 },
                ]
            } else {
                vec![
                    SynthVoice { start_ms: 0, dur_ms: 65, f0: base * 1.22, f1: base * 1.15, amp: 4600.0, harm2: 0.22, harm3: 0.04, decay: 3.6 },
                    SynthVoice { start_ms: 42, dur_ms: 110, f0: base * 0.96, f1: base * 0.92, amp: 4800.0, harm2: 0.16, harm3: 0.03, decay: 3.4 },
                ]
            }
        }
        // Futuristic 4-note rising major-9th arpeggio for Bluetooth ON
        "bt_on" => vec![
            SynthVoice { start_ms: 0, dur_ms: 90, f0: 587.33, f1: 587.33, amp: 4200.0, harm2: 0.24, harm3: 0.08, decay: 2.6 },
            SynthVoice { start_ms: 58, dur_ms: 100, f0: 739.99, f1: 739.99, amp: 4400.0, harm2: 0.22, harm3: 0.08, decay: 2.6 },
            SynthVoice { start_ms: 116, dur_ms: 130, f0: 880.00, f1: 880.00, amp: 4600.0, harm2: 0.18, harm3: 0.10, decay: 2.8 },
            SynthVoice { start_ms: 170, dur_ms: 190, f0: 1318.51, f1: 1318.51, amp: 3600.0, harm2: 0.12, harm3: 0.05, decay: 3.2 },
            SynthVoice { start_ms: 170, dur_ms: 190, f0: 587.33, f1: 587.33, amp: 2400.0, harm2: 0.10, harm3: 0.0, decay: 3.0 },
        ],
        // Smooth descending triad power-down for Bluetooth OFF
        "bt_off" => vec![
            SynthVoice { start_ms: 0, dur_ms: 85, f0: 880.00, f1: 865.0, amp: 4200.0, harm2: 0.18, harm3: 0.05, decay: 2.8 },
            SynthVoice { start_ms: 60, dur_ms: 95, f0: 659.25, f1: 645.0, amp: 4200.0, harm2: 0.16, harm3: 0.04, decay: 3.0 },
            SynthVoice { start_ms: 120, dur_ms: 155, f0: 440.00, f1: 415.0, amp: 4000.0, harm2: 0.12, harm3: 0.0, decay: 3.2 },
        ],
        // Upbeat staccato forward chirp for next track
        "next_track" => vec![
            SynthVoice { start_ms: 0, dur_ms: 52, f0: 659.25, f1: 740.0, amp: 4600.0, harm2: 0.25, harm3: 0.10, decay: 4.0 },
            SynthVoice { start_ms: 50, dur_ms: 85, f0: 987.77, f1: 1046.5, amp: 4800.0, harm2: 0.20, harm3: 0.08, decay: 3.5 },
        ],
        // Rewind double-tap chirp for previous track
        "prev_track" => vec![
            SynthVoice { start_ms: 0, dur_ms: 52, f0: 987.77, f1: 880.0, amp: 4600.0, harm2: 0.22, harm3: 0.08, decay: 4.0 },
            SynthVoice { start_ms: 50, dur_ms: 85, f0: 659.25, f1: 587.33, amp: 4800.0, harm2: 0.18, harm3: 0.06, decay: 3.5 },
        ],
        // Warm major-third bloom for play/resume
        "play" => vec![
            SynthVoice { start_ms: 0, dur_ms: 110, f0: 523.25, f1: 523.25, amp: 3800.0, harm2: 0.20, harm3: 0.06, decay: 2.8 },
            SynthVoice { start_ms: 28, dur_ms: 120, f0: 659.25, f1: 659.25, amp: 4000.0, harm2: 0.18, harm3: 0.06, decay: 2.8 },
            SynthVoice { start_ms: 62, dur_ms: 130, f0: 783.99, f1: 783.99, amp: 3400.0, harm2: 0.12, harm3: 0.04, decay: 3.2 },
        ],
        // Soft damped settle for pause
        "pause" => vec![
            SynthVoice { start_ms: 0, dur_ms: 75, f0: 783.99, f1: 770.0, amp: 4200.0, harm2: 0.15, harm3: 0.04, decay: 3.4 },
            SynthVoice { start_ms: 55, dur_ms: 110, f0: 587.33, f1: 575.0, amp: 3800.0, harm2: 0.12, harm3: 0.0, decay: 3.6 },
        ],
        // Subtle, quiet low dismiss for stop/cancel
        "cancel" => vec![
            SynthVoice { start_ms: 0, dur_ms: 65, f0: 440.00, f1: 415.0, amp: 3000.0, harm2: 0.12, harm3: 0.0, decay: 3.8 },
            SynthVoice { start_ms: 48, dur_ms: 95, f0: 329.63, f1: 311.13, amp: 2800.0, harm2: 0.10, harm3: 0.0, decay: 4.0 },
        ],
        // Bright confirmation chime for setting a timer
        "timer_set" => vec![
            SynthVoice { start_ms: 0, dur_ms: 70, f0: 659.25, f1: 659.25, amp: 4400.0, harm2: 0.24, harm3: 0.08, decay: 3.4 },
            SynthVoice { start_ms: 55, dur_ms: 70, f0: 880.00, f1: 880.00, amp: 4600.0, harm2: 0.20, harm3: 0.08, decay: 3.2 },
            SynthVoice { start_ms: 110, dur_ms: 130, f0: 1174.66, f1: 1174.66, amp: 4000.0, harm2: 0.15, harm3: 0.06, decay: 3.0 },
        ],
        // Warm morning chord for setting an alarm
        "alarm_set" => vec![
            SynthVoice { start_ms: 0, dur_ms: 90, f0: 523.25, f1: 523.25, amp: 4200.0, harm2: 0.22, harm3: 0.08, decay: 2.6 },
            SynthVoice { start_ms: 65, dur_ms: 95, f0: 659.25, f1: 659.25, amp: 4400.0, harm2: 0.20, harm3: 0.08, decay: 2.6 },
            SynthVoice { start_ms: 130, dur_ms: 165, f0: 1046.50, f1: 1046.50, amp: 4200.0, harm2: 0.16, harm3: 0.06, decay: 2.8 },
            SynthVoice { start_ms: 130, dur_ms: 165, f0: 783.99, f1: 783.99, amp: 2800.0, harm2: 0.10, harm3: 0.0, decay: 2.8 },
        ],
        // Repeating bell pattern for timer completion
        "timer_ring" => vec![
            SynthVoice { start_ms: 0, dur_ms: 110, f0: 880.00, f1: 880.00, amp: 5200.0, harm2: 0.25, harm3: 0.12, decay: 3.0 },
            SynthVoice { start_ms: 120, dur_ms: 110, f0: 1174.66, f1: 1174.66, amp: 5200.0, harm2: 0.22, harm3: 0.12, decay: 3.0 },
            SynthVoice { start_ms: 250, dur_ms: 180, f0: 880.00, f1: 880.00, amp: 4800.0, harm2: 0.22, harm3: 0.10, decay: 2.6 },
            SynthVoice { start_ms: 250, dur_ms: 180, f0: 1318.51, f1: 1318.51, amp: 3600.0, harm2: 0.14, harm3: 0.06, decay: 2.8 },
        ],
        // Melodic gentle wake-up alarm phrase
        "alarm_ring" => vec![
            SynthVoice { start_ms: 0, dur_ms: 120, f0: 523.25, f1: 523.25, amp: 4800.0, harm2: 0.24, harm3: 0.08, decay: 2.5 },
            SynthVoice { start_ms: 110, dur_ms: 120, f0: 659.25, f1: 659.25, amp: 5000.0, harm2: 0.22, harm3: 0.08, decay: 2.5 },
            SynthVoice { start_ms: 220, dur_ms: 130, f0: 783.99, f1: 783.99, amp: 5200.0, harm2: 0.20, harm3: 0.10, decay: 2.4 },
            SynthVoice { start_ms: 330, dur_ms: 220, f0: 1046.50, f1: 1046.50, amp: 5000.0, harm2: 0.16, harm3: 0.08, decay: 2.4 },
        ],
        // Short clean UI tick for time/date/query
        "tick" => vec![
            SynthVoice { start_ms: 0, dur_ms: 55, f0: 783.99, f1: 810.0, amp: 3800.0, harm2: 0.18, harm3: 0.05, decay: 4.5 },
        ],
        // Sparkling chord for LED ring color change
        "ring_mode" => vec![
            SynthVoice { start_ms: 0, dur_ms: 85, f0: 587.33, f1: 587.33, amp: 3800.0, harm2: 0.22, harm3: 0.08, decay: 3.0 },
            SynthVoice { start_ms: 40, dur_ms: 115, f0: 880.00, f1: 880.00, amp: 4000.0, harm2: 0.18, harm3: 0.10, decay: 3.0 },
        ],
        // Soft polite double-tap when remote LLM server is offline
        "offline" => vec![
            SynthVoice { start_ms: 0, dur_ms: 70, f0: 392.00, f1: 370.0, amp: 3200.0, harm2: 0.12, harm3: 0.0, decay: 3.8 },
            SynthVoice { start_ms: 95, dur_ms: 95, f0: 329.63, f1: 311.13, amp: 3000.0, harm2: 0.10, harm3: 0.0, decay: 4.0 },
        ],
        _ => vec![
            SynthVoice { start_ms: 0, dur_ms: 85, f0: 587.33, f1: 587.33, amp: 4200.0, harm2: 0.18, harm3: 0.05, decay: 3.0 },
        ],
    };
    render_synth_chime(&voices)
}

fn play_pcm_48k(pcm: &[u8]) {
    if pcm.is_empty() {
        return;
    }
    if let Ok(mut child) = Command::new("aplay")
        .args(["-q", "-t", "raw", "-f", "S16_LE", "-r", "48000", "-c", "1"])
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    {
        if let Some(mut sin) = child.stdin.take() {
            let _ = sin.write_all(pcm);
        }
        let _ = child.wait();
    }
    drain_capture_fd_now();
}


/// Stream 24 kHz S16_LE PCM16 directly from POST /tts/stream into aplay (~0.6s TTFA).
/// Returns Ok(true) if completed, Ok(false) if interrupted, Err(...) if endpoint unavailable.
fn stream_tts_and_play(
    base: &str,
    token: &str,
    ca: &str,
    text: &str,
    emotion: &str,
    keep_visual: bool,
    timeout_secs: u64,
) -> Result<bool, String> {
    let auth = format!("Bearer {token}");
    let ident = crate::hardware::device_identity();
    let mut h = vec![
        ("Content-Type", "application/json"),
        ("Accept", "audio/pcm"),
        ("X-Atri-Device-Id", ident.device_id.as_str()),
        ("X-Atri-Device-Key", ident.device_key.as_str()),
        ("X-Atri-Hw-Hash", ident.hw_hash.as_str()),
    ];
    if !token.is_empty() {
        h.push(("Authorization", auth.as_str()));
    }
    let ca_opt = if ca.is_empty() { None } else { Some(ca) };
    let payload = obj(vec![
        ("text", Json::Str(text.to_string())),
        ("emotion", Json::Str(emotion.to_string())),
        ("speaker", Json::Str("AtriVoice".to_string())),
    ])
    .to_string_compact();

    let t0 = Instant::now();
    let mut aplay: Option<Child> = None;
    let mut carry: Option<u8> = None;
    let mut total_bytes: usize = 0;
    let mut interrupted = false;
    let mut visual_started = false;

    let cancel_cb = || BUTTON_TRIGGERED.load(Ordering::SeqCst);
    let stream_res = request_stream(
        &format!("{base}/tts/stream"),
        Some(payload.as_bytes()),
        &h,
        ca_opt,
        timeout_secs,
        16 * 1024 * 1024,
        Some(&cancel_cb),
        |chunk| {
            if chunk.is_empty() {
                return Ok(());
            }
            if BUTTON_TRIGGERED.load(Ordering::SeqCst) {
                interrupted = true;
                return Err("interrupted".into());
            }
            if aplay.is_none() {
                let ttfa_ms = t0.elapsed().as_millis();
                println!("[tts-stream] TTFA={ttfa_ms}ms emotion={emotion}");
                send_bt_cmd("duck");
                if !keep_visual {
                    set_vad_visual("speaking", false, emotion);
                    visual_started = true;
                }
                let child = Command::new("aplay")
                    .args(["-q", "-f", "S16_LE", "-r", "24000", "-c", "1", "-t", "raw", "-"])
                    .stdin(Stdio::piped())
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .spawn()
                    .map_err(|e| format!("aplay spawn: {e}"))?;
                aplay = Some(child);
            }
            let mut aligned: Vec<u8> = Vec::with_capacity(chunk.len() + 1);
            if let Some(b) = carry.take() {
                aligned.push(b);
            }
            aligned.extend_from_slice(chunk);
            if aligned.len() % 2 == 1 {
                carry = aligned.pop();
            }
            if aligned.is_empty() {
                return Ok(());
            }
            let mut sq = 0.0f64;
            let n_samp = aligned.len() / 2;
            for pair in aligned.chunks_exact(2) {
                let s = i16::from_le_bytes([pair[0], pair[1]]) as f64;
                sq += s * s;
            }
            let rms = (sq / n_samp.max(1) as f64).sqrt();
            let lvl = (rms / 9000.0).clamp(0.0, 1.0);
            send_voice_level(Some(lvl));

            if let Some(ref mut ch) = aplay {
                if let Some(ref mut stdin) = ch.stdin {
                    stdin.write_all(&aligned).map_err(|e| format!("aplay write: {e}"))?;
                }
            }
            total_bytes += aligned.len();
            Ok(())
        },
    );

    if let Some(mut ch) = aplay {
        drop(ch.stdin.take());
        if interrupted {
            let _ = ch.kill();
        }
        let _ = ch.wait();
    }
    if visual_started {
        send_voice_level(None);
        send_bt_cmd("unduck");
        if !keep_visual {
            set_vad_visual("idle", false, "default");
        }
    } else if total_bytes > 0 {
        send_voice_level(None);
        send_bt_cmd("unduck");
    }

    if interrupted {
        return Ok(false);
    }
    match stream_res {
        Ok(()) if total_bytes > 0 => {
            println!(
                "[tts-stream] finished {} bytes in {}ms",
                total_bytes,
                t0.elapsed().as_millis()
            );
            Ok(true)
        }
        Ok(()) => Err("empty tts stream".into()),
        Err(e) => Err(e),
    }
}

fn play_spoken_wav(audio: &[u8], text: &str, emotion: &str, keep_visual: bool, base: &str, token: &str, ca: &str) {
    send_bt_cmd("duck");
    if !keep_visual {
        set_vad_visual("speaking", false, emotion);
    }
    let _ = play_audio_bytes(audio, text, base, token, ca);
    send_bt_cmd("unduck");
    if !keep_visual {
        set_vad_visual("idle", false, "default");
    }
}

const YMUSIC_DUCK_FILE: &str = "/var/run/atrios/ymusic_duck";

struct MusicDuckGuard;
impl MusicDuckGuard {
    fn acquire() -> Self {
        let _ = fs::create_dir_all("/var/run/atrios");
        let _ = fs::write(YMUSIC_DUCK_FILE, b"1");
        Self
    }
}
impl Drop for MusicDuckGuard {
    fn drop(&mut self) {
        let _ = fs::remove_file(YMUSIC_DUCK_FILE);
    }
}

pub fn speak_via_remote_tts(text: &str, emotion: &str, keep_visual: bool) -> bool {
    let _duck = MusicDuckGuard::acquire();
    let cfg = load_config();
    let base = cfg.server.trim_end_matches('/').to_string();
    let has_remote = require_https(&cfg.server).is_ok();
    let now_unix = now_unix_i64();
    let remote_ok = has_remote && now_unix >= REMOTE_DOWN_UNTIL.load(Ordering::Relaxed);

    if remote_ok {
        match stream_tts_and_play(&base, &cfg.token, &cfg.ca_file, text, emotion, keep_visual, 3) {
            Ok(_) => return true,
            Err(e) if !e.contains("404") => {
                REMOTE_DOWN_UNTIL.store(now_unix_i64() + 45, Ordering::Relaxed);
                println!("[local-ai] GPU TTS unreachable ({e}); instant offline fallback 45s");
            }
            Err(_) => {
                // Only fall back to legacy /tts WAV when server returned 404 (older server)
                let tok = cfg.token.clone();
        let ca = cfg.ca_file.clone();
        let auth = format!("Bearer {tok}");
        let mut h = vec![("Content-Type", "application/json"), ("Accept", "audio/wav")];
        if !tok.is_empty() {
            h.push(("Authorization", auth.as_str()));
        }
        let ca_opt = if ca.is_empty() { None } else { Some(ca.as_str()) };
        let payload = obj(vec![
            ("text", Json::Str(text.to_string())),
            ("emotion", Json::Str(emotion.to_string())),
        ])
        .to_string_compact();
        match request_bytes(
            &format!("{base}/tts"),
            Some(payload.as_bytes()),
            &h,
            ca_opt,
            8,
            4 * 1024 * 1024,
            None,
        ) {
            Ok(b) if parse_wav(&b).is_ok() => {
                println!("[local-ai] GPU TTS speaking ({} bytes): {text:?}", b.len());
                play_spoken_wav(&b, text, emotion, keep_visual, &base, &cfg.token, &cfg.ca_file);
                return true;
            }
            Ok(_) => {}
            Err(e) => {
                REMOTE_DOWN_UNTIL.store(now_unix_i64() + 45, Ordering::Relaxed);
                println!("[local-ai] GPU TTS unreachable ({e}); offline fallback 45s");
            }
        }
            }
        }
    }

    // Offline AtriVoice phrase bank / local TTS fallback (zero Python, instant)
    if let Some(wav) = crate::local_tts::synthesize_offline_wav(text) {
        println!("[local-tts] offline AtriVoice speaking ({} bytes): {text:?}", wav.len());
        play_spoken_wav(&wav, text, emotion, keep_visual, &base, &cfg.token, &cfg.ca_file);
        return true;
    }
    false
}

pub fn send_bt_cmd(cmd: &str) {
    if Path::new(BT_CMD_SOCK).exists() {
        if let Ok(sock) = UnixDatagram::unbound() {
            let _ = sock.send_to(cmd.as_bytes(), BT_CMD_SOCK);
        }
    }
}

pub fn play_chime_interruptible(action: &str, stop_flag: &AtomicBool, pid_slot: &AtomicI32) {
    if stop_flag.load(Ordering::SeqCst) {
        return;
    }
    let pcm = build_chime_pcm(action, None);
    if pcm.is_empty() {
        return;
    }
    if let Ok(mut child) = Command::new("aplay")
        .args(["-q", "-t", "raw", "-f", "S16_LE", "-r", "48000", "-c", "1"])
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    {
        pid_slot.store(child.id() as i32, Ordering::SeqCst);
        if let Some(mut sin) = child.stdin.take() {
            // Write in small 100ms chunks so stop_flag aborts immediately
            let chunk_bytes = 48000 * 2 / 10;
            for chunk in pcm.chunks(chunk_bytes) {
                if stop_flag.load(Ordering::SeqCst) {
                    let _ = child.kill();
                    break;
                }
                if sin.write_all(chunk).is_err() {
                    break;
                }
            }
        }
        let _ = child.wait();
        pid_slot.store(0, Ordering::SeqCst);
    }
}

pub fn play_command_chime(action: &str) {
    let pcm = build_chime_pcm(action, None);
    play_pcm_48k(&pcm);
}

fn play_volume_chime(cur: i32, nxt: i32) {
    let action = if nxt < cur || nxt == 0 { "volume_down" } else { "volume_up" };
    let pcm = build_chime_pcm(action, Some(nxt));
    play_pcm_48k(&pcm);
}

pub fn show_music_icon_async(kind: &'static str) {
    thread::spawn(move || {
        let _ = fs::create_dir_all("/var/run/atrios");
        let _ = fs::write(SCREEN_MODE_FILE, "music_icon");
        let _ = Command::new("pkill")
            .args(["-f", "/usr/lib/atrios/light screen"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        let _ = Command::new("/usr/lib/atrios/light")
            .args(["screen", "music", kind])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        if fs::read_to_string(SCREEN_MODE_FILE)
            .map(|s| s.trim() == "music_icon")
            .unwrap_or(false)
        {
            let _ = fs::write(SCREEN_MODE_FILE, "clock");
        }
    });
}

fn execute_device_command(cmd: DeviceCommand) {
    match cmd {
        DeviceCommand::NextTrack | DeviceCommand::PrevTrack | DeviceCommand::Pause | DeviceCommand::Play => {
            let (sock_cmd, ch) = match cmd {
                DeviceCommand::NextTrack => ("next", "next_track"),
                DeviceCommand::PrevTrack => ("prev", "prev_track"),
                DeviceCommand::Pause => ("pause", "pause"),
                DeviceCommand::Play => ("play", "play"),
                _ => unreachable!(),
            };
            if crate::ymusic::is_active() {
                let _ = crate::ymusic::trigger_control(sock_cmd);
            } else if Path::new(BT_CMD_SOCK).exists() {
                if let Ok(sock) = UnixDatagram::unbound() {
                    let _ = sock.send_to(sock_cmd.as_bytes(), BT_CMD_SOCK);
                }
            }
            let icon = match cmd {
                DeviceCommand::NextTrack => "next",
                DeviceCommand::PrevTrack => "prev",
                DeviceCommand::Pause => "pause",
                DeviceCommand::Play => "play",
                _ => "play",
            };
            show_music_icon_async(icon);
            play_command_chime(ch);
        }
        DeviceCommand::VolumeUp(step) => {
            let cur = fs::read_to_string(VOLUME_FILE)
                .ok()
                .and_then(|s| s.trim().parse::<i32>().ok())
                .unwrap_or(10);
            let nxt = (cur + step).clamp(0, 100);
            let _ = Command::new("/usr/bin/atrios")
                .args(["volume", &nxt.to_string()])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            println!("[atriai] volume: {cur}% -> {nxt}%");
            play_volume_chime(cur, nxt);
        }
        DeviceCommand::VolumeDown(step) => {
            let cur = fs::read_to_string(VOLUME_FILE)
                .ok()
                .and_then(|s| s.trim().parse::<i32>().ok())
                .unwrap_or(10);
            let nxt = (cur - step).clamp(0, 100);
            let _ = Command::new("/usr/bin/atrios")
                .args(["volume", &nxt.to_string()])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            println!("[atriai] volume: {cur}% -> {nxt}%");
            play_volume_chime(cur, nxt);
        }
        DeviceCommand::VolumeSet(target) => {
            let cur = fs::read_to_string(VOLUME_FILE)
                .ok()
                .and_then(|s| s.trim().parse::<i32>().ok())
                .unwrap_or(10);
            let nxt = target.clamp(0, 100);
            let _ = Command::new("/usr/bin/atrios")
                .args(["volume", &nxt.to_string()])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            println!("[atriai] volume: {cur}% -> {nxt}%");
            play_volume_chime(cur, nxt);
        }
        DeviceCommand::BluetoothOn => {
            set_vad_visual("bt_on", false, "default");
            let _ = Command::new("/bin/sh")
                .args([
                    "-c",
                    "/usr/lib/atrios/wireless bt speaker on >>/var/log/atriai.log 2>&1 && /usr/lib/atrios/wireless bt pair 120 >>/var/log/atriai.log 2>&1",
                ])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn();
            println!("[atriai] bluetooth: speaker ON + pairing window open (120s) + bt_on animation");
            if !speak_via_remote_tts("Блютуз включён.", "neutral", true) {
                play_command_chime("bt_on");
            }
            thread::sleep(Duration::from_millis(1200));
            drain_capture_fd_now();
        }
        DeviceCommand::BluetoothOff => {
            if Path::new(BT_CMD_SOCK).exists() {
                if let Ok(sock) = UnixDatagram::unbound() {
                    let _ = sock.send_to(b"pause", BT_CMD_SOCK);
                    let _ = sock.send_to(b"speaker_off", BT_CMD_SOCK);
                }
            }
            let _ = Command::new("/bin/sh")
                .args([
                    "-c",
                    "/usr/lib/atrios/wireless bt speaker off >>/var/log/atriai.log 2>&1 && /usr/lib/atrios/wireless bt hide >>/var/log/atriai.log 2>&1",
                ])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn();
            println!("[atriai] bluetooth: speaker OFF (hidden)");
            if !speak_via_remote_tts("Блютуз выключен.", "neutral", false) {
                play_command_chime("bt_off");
            }
        }
    }
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

pub fn prepare_asr_wav(pcm: &[u8]) -> Vec<u8> {
    let lead_samples = 1280usize; // 80 ms @ 16 kHz
    let min_total_samples = 14400usize; // 0.90 s @ 16 kHz
    let samples: Vec<i16> = pcm
        .chunks_exact(2)
        .map(|b| i16::from_le_bytes([b[0], b[1]]))
        .collect();
    if samples.is_empty() {
        return encode_wav_pcm16(&vec![0u8; min_total_samples * 2], SR as u32, 1);
    }
    let sum: f64 = samples.iter().map(|&s| s as f64).sum();
    let mean = sum / (samples.len() as f64);
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
    let out_samples = (lead_samples + centered.len()).max(min_total_samples);
    let mut out = vec![0u8; out_samples * 2];
    for (idx, v) in centered.into_iter().enumerate() {
        let s = (v * gain).round().clamp(-32767.0, 32767.0) as i16;
        let off = (lead_samples + idx) * 2;
        out[off..off + 2].copy_from_slice(&s.to_le_bytes());
    }
    encode_wav_pcm16(&out, SR as u32, 1)
}

fn send_phrase_to_server(
    server_url: &str,
    pcm: &[u8],
    wake_word: &str,
    force_active: bool,
) -> Result<(String, String, bool), String> {
    if pcm.len() > SR * 2 * 30 || pcm.len() % 2 != 0 {
        return Err("Invalid or oversized PCM16 phrase".into());
    }
    let has_remote = require_https(server_url).is_ok();
    let use_local_stt = crate::local_stt::is_available();
    if !use_local_stt && !has_remote {
        return Err("No local STT model and no HTTPS server configured".into());
    }
    let cfg = load_config();
    let base = server_url.trim_end_matches('/').to_string();
    let mut keep_listening = false;
    let mut user_addressed = force_active || wake_word.is_empty();

    let res: Result<(String, String, bool), String> = (|| {
        let mut had_unk = false;
        let text = if use_local_stt {
            let t0 = Instant::now();
            let (t, unk) = crate::local_stt::transcribe_command_pcm16(pcm)?;
            had_unk = unk;
            println!("[local-stt] fast ({}ms, unk={unk}) text={t:?}", t0.elapsed().as_millis());
            t
        } else {
            let wav = prepare_asr_wav(pcm);
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
            let t = rec.get_str("text").ok_or("Invalid ASR response")?.trim().to_string();
            println!("[stt] text={t:?}");
            t
        };

        if crate::local_ai::is_ringing()
            && (is_cancel_phrase(&text)
                || text.contains("стоп")
                || text.contains("выключи")
                || text.contains("отключи")
                || text.contains("выруби")
                || text.contains("отмена")
                || text.contains("хватит")
                || text.contains("тихо")
                || text.contains("замолчи")
                || text.contains("таймер")
                || text.contains("будильник"))
        {
            crate::local_ai::stop_ringing();
            keep_listening = false;
            return Ok((text, String::new(), false));
        }

        // Direct Bluetooth fast-path: bypasses wake-word loss, mini-AI, and remote LLM completely (<100ms).
        if let Some(bt_cmd) = is_direct_bluetooth_command(&text) {
            user_addressed = true;
            println!("[atriai] direct fast-path bluetooth command ({text:?}) -> {bt_cmd:?}");
            execute_device_command(bt_cmd);
            set_error("voice", None, "", 15);
            keep_listening = false;
            return Ok((text, String::new(), false));
        }

        if text.is_empty() {
            set_error("voice", None, "", 15);
            if force_active {
                keep_listening = true;
                set_vad_visual("vad", false, "default");
                return Ok((text, String::new(), true));
            }
            return Ok((text, String::new(), false));
        }
        if !wake_word.is_empty() && !matches_wake_word(&text, wake_word) {
            println!("[wake] ignored background speech ({wake_word:?}): {text:?}");
            set_error("voice", None, "", 15);
            return Ok((text, String::new(), false));
        }
        // Two-stage offline wake verification: constrained grammar (cmd_rec) can hallucinate
        // wake proxies ("а три", "арти") on casual out-of-vocabulary speech. Verify with the
        // unconstrained Russian Vosk model before lighting up the LED ring or acting.
        let mut verified_wake_only = is_wake_word_only(&text, wake_word);
        if use_local_stt && !wake_word.is_empty() {
            if let Ok(full_text) = crate::local_stt::transcribe_pcm16(pcm) {
                if !matches_wake_word(&full_text, wake_word) {
                    println!(
                        "[wake] rejected false-positive offline match (fast={text:?}, unk={had_unk}, full={full_text:?})"
                    );
                    set_error("voice", None, "", 15);
                    return Ok((String::new(), String::new(), false));
                }
                if verified_wake_only {
                    let pcm_ms = (pcm.len() * 1000) / (SR * 2);
                    if had_unk || pcm_ms > 2400 || !is_wake_word_only(&full_text, wake_word) {
                        verified_wake_only = false;
                    }
                }
            }
        }
        user_addressed = true;
        if verified_wake_only {
            WAKE_TRIGGERED.store(true, Ordering::SeqCst);
            keep_listening = true;
            println!("[wake] activated ({text:?}) -> ActiveCommand (7s)");
            send_bt_cmd("duck");
            set_vad_visual("vad", false, "default");
            play_command_chime("wake");
            drain_capture_fd_now();
            set_error("voice", None, "", 15);
            return Ok((text, String::new(), true));
        }
        let (clean_query, has_trailing_stop) = split_trailing_stop(&text, wake_word);
        if is_cancel_phrase(&clean_query) {
            println!("[atriai] cancelled by phrase ({text:?}) -> idle");
            keep_listening = false;
            play_command_chime("cancel");
            set_error("voice", None, "", 15);
            return Ok((text, String::new(), false));
        }
        if let Some(intent) = crate::local_ai::parse_local_intent(&text, wake_word) {
            let is_timer_ui = matches!(
                intent,
                crate::local_ai::LocalIntent::SetTimer { .. }
                    | crate::local_ai::LocalIntent::ShowTimer { .. }
                    | crate::local_ai::LocalIntent::QueryTimers
                    | crate::local_ai::LocalIntent::CancelTimers { .. }
            );
            crate::local_ai::execute_local_intent(intent);
            set_error("voice", None, "", 15);
            if is_timer_ui {
                // Keep LED matrix on timer display (no 3-dots vad/thinking takeover)
                keep_listening = false;
                return Ok((text, String::new(), false));
            }
            if !has_trailing_stop && force_active {
                keep_listening = true;
                set_vad_visual("vad", false, "default");
                return Ok((text, String::new(), true));
            }
            keep_listening = false;
            return Ok((text, String::new(), false));
        }
        // If user said "поставь <song>" and constrained grammar hit [unk], re-transcribe unconstrained for song title
        let ym_text = if use_local_stt && had_unk && (clean_query.starts_with("поставь") || clean_query.starts_with("включи песню") || clean_query.starts_with("включи трек")) {
            crate::local_stt::transcribe_pcm16(pcm).unwrap_or_else(|_| text.clone())
        } else {
            text.clone()
        };
        if let Some(ym_cmd) = crate::ymusic::parse_ymusic_voice_command(&ym_text, wake_word) {
            println!("[ymusic] voice command: {ym_cmd:?}");
            crate::ymusic::execute_voice_command(ym_cmd);
            play_command_chime("play");
            set_error("voice", None, "", 15);
            if !has_trailing_stop && force_active {
                keep_listening = true;
                set_vad_visual("vad", false, "default");
                return Ok((ym_text, String::new(), true));
            }
            keep_listening = false;
            return Ok((ym_text, String::new(), false));
        }
        if crate::ymusic::is_active()
            && (clean_query.contains("выключи музыку") || clean_query.contains("останови музыку"))
        {
            let _ = crate::ymusic::trigger_control("stop");
            play_command_chime("pause");
            set_error("voice", None, "", 15);
            keep_listening = false;
            return Ok((text, String::new(), false));
        }
        if let Some(cmd) = local_command(&text, wake_word) {
            execute_device_command(cmd);
            set_error("voice", None, "", 15);
            if !has_trailing_stop && force_active {
                keep_listening = true;
                set_vad_visual("vad", false, "default");
                return Ok((text, String::new(), true));
            }
            keep_listening = false;
            return Ok((text, String::new(), false));
        }

        let now_unix = now_unix_i64();
        let remote_backed_off = now_unix < REMOTE_DOWN_UNTIL.load(Ordering::Relaxed);
        if !has_remote || remote_backed_off {
            println!("[local-ai] unhandled phrase in offline mode (backed_off={remote_backed_off}): {clean_query:?}");
            if !speak_via_remote_tts("Извини, сервер сейчас недоступен, я работаю в офлайн-режиме.", "neutral", false) {
                play_command_chime("offline");
            }
            keep_listening = false;
            return Ok((text, String::new(), false));
        }

        // Only run the heavier unconstrained Vosk pass when forwarding a free-form question to remote LLM.
        let mut q_text = if clean_query.is_empty() {
            text.clone()
        } else {
            clean_query.clone()
        };
        if use_local_stt && had_unk {
            if let Ok(full_text) = crate::local_stt::transcribe_pcm16(pcm) {
                let (full_clean, _) = split_trailing_stop(&full_text, wake_word);
                if !full_clean.is_empty() {
                    println!("[local-stt] unconstrained fallback text={full_text:?}");
                    q_text = full_clean;
                }
            }
        }

        set_vad_visual("thinking", false, "default");
        let base2 = base.clone();
        let tok2 = cfg.token.clone();
        let ca2 = cfg.ca_file.clone();
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
        println!("[chat] answer={answer:?} emotion={emotion}");

        write_status("speaking", &text, &answer, "");
        let completed = match stream_tts_and_play(&base, &cfg.token, &cfg.ca_file, &answer, &emotion, false, 25) {
            Ok(done) => done,
            Err(stream_err) => {
                println!("[tts-stream] fallback to /tts WAV ({stream_err})");
                let base3 = base.clone();
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
                send_bt_cmd("duck");
                set_vad_visual("speaking", false, &emotion);
                println!("[tts] playing {} bytes", audio.len());
                play_audio_bytes(&audio, &answer, &base, &cfg.token, &cfg.ca_file)?
            }
        };
        set_error("voice", None, "", 120);
        if completed && !has_trailing_stop {
            keep_listening = true;
            set_vad_visual("vad", false, "default");
            Ok((text, answer, true))
        } else {
            keep_listening = false;
            println!(
                "[atriai] conversation ended (trailing_stop={has_trailing_stop}, completed={completed}) -> clock"
            );
            Ok((text, answer, false))
        }
    })();

    if let Err(ref e) = res {
        if user_addressed && !e.starts_with("InterruptedError") {
            if use_local_stt {
                REMOTE_DOWN_UNTIL.store(now_unix_i64() + 45, Ordering::Relaxed);
                println!("[local-ai] remote AtriAI unreachable ({e}); offline backoff 45s");
                play_command_chime("offline");
            } else {
                set_error("voice", Some(error_kind_from_msg(e, "server")), e, 15);
            }
        }
    }
    if !keep_listening {
        send_bt_cmd("unduck");
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

#[derive(Clone, Copy, PartialEq, Eq)]
enum PipelineMode {
    WakeSpotter,
    ActiveCommand,
}

impl PipelineMode {
    fn as_str(self) -> &'static str {
        match self {
            PipelineMode::WakeSpotter => "WakeSpotter",
            PipelineMode::ActiveCommand => "ActiveCommand",
        }
    }
}

struct VadProcessor {
    noise_rms: f64,
    wait_quiet_frames: usize,
}

impl VadProcessor {
    fn new() -> Self {
        Self {
            noise_rms: 160.0,
            wait_quiet_frames: 0,
        }
    }

    fn warmup(&mut self, rms: f64) {
        self.noise_rms = (self.noise_rms * 0.85 + rms * 0.15).clamp(60.0, 1800.0);
    }

    fn update_noise(&mut self, rms: f64) {
        if rms < self.noise_rms {
            self.noise_rms = (self.noise_rms * 0.95 + rms * 0.05).clamp(60.0, 1800.0);
        } else {
            self.noise_rms =
                (self.noise_rms * 0.98 + rms.min(self.noise_rms * 3.0) * 0.02).clamp(60.0, 1800.0);
        }
    }

    fn adapt_rejected(&mut self, mean_rms: f64) {
        self.noise_rms = (self.noise_rms * 0.70 + mean_rms * 0.30).clamp(60.0, 1800.0);
    }

    fn thresholds(&self, mode: PipelineMode, vad_thr: f64, silence_cfg: f64) -> (f64, f64, f64, f64) {
        match mode {
            PipelineMode::WakeSpotter => {
                let start = (self.noise_rms * 2.35 + 130.0).clamp(vad_thr, 2600.0);
                let keep = (self.noise_rms * 1.55 + 75.0).clamp(vad_thr * 0.68, start * 0.80);
                (start, keep, 3200.0, 420.0)
            }
            PipelineMode::ActiveCommand => {
                let start = (self.noise_rms * 1.75 + 85.0).clamp(vad_thr * 0.72, 2200.0);
                let keep = (self.noise_rms * 1.30 + 50.0).clamp(vad_thr * 0.55, start * 0.78);
                (start, keep, 8000.0, silence_cfg)
            }
        }
    }
}

fn is_valid_speech(frame_rms: &[f64], noise_rms: f64, start_thr: f64, music_on: bool) -> bool {
    if frame_rms.is_empty() {
        return false;
    }
    let mut min_r = f64::MAX;
    let mut max_r = 0.0f64;
    for &r in frame_rms {
        if r < min_r {
            min_r = r;
        }
        if r > max_r {
            max_r = r;
        }
    }
    if music_on {
        max_r >= start_thr * 1.03 && max_r >= noise_rms * 1.65 + 80.0 && max_r >= min_r * 1.22
    } else {
        max_r >= start_thr * 1.08 && max_r >= noise_rms * 2.1 + 100.0 && max_r >= min_r * 1.45
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
    let _ = crate::npu::init();
    let local_ok = crate::local_stt::init();
    crate::local_ai::start_scheduler();
    println!(
        "[atriai] Starting voice client -> {} (local_mini_ai={})",
        cfg0.server,
        if local_ok { "ready" } else { "off" }
    );
    write_status("listening", "", "", "");

    let mut vad = VadProcessor::new();
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
            vad.wait_quiet_frames = 0;
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
        let mut utt_rms: Vec<f64> = Vec::new();
        let mut pre_roll: VecDeque<(Vec<u8>, f64)> = VecDeque::with_capacity(8);
        let mut speaking = false;
        let mut utt_peak = 0.0f64;
        let mut speech_hits = 0usize;
        let mut hit_max_utt = false;
        let mut warmup_frames = if first_start { 15usize } else { 0 };
        let mut force_active = pend_force_active;
        pend_force_active = false;
        let mut mode = if force_active {
            vad.wait_quiet_frames = 0;
            PipelineMode::ActiveCommand
        } else {
            PipelineMode::WakeSpotter
        };
        let ww_cfg = cfg.wake_word.trim().to_string();
        let wake_required = !ww_cfg.is_empty() && !matches!(ww_cfg.to_ascii_lowercase().as_str(), "off" | "none" | "false");
        let mut wait_speech_ms = 0.0f64;
        let mut silence_ms = 0.0f64;
        let frame_ms = (FRAME_SAMPLES as f64 / SR as f64) * 1000.0;
        let vad_thr = cfg.vad_threshold;
        let min_bytes = (SR * 2) * (cfg.min_speech_ms as usize) / 1000;
        let mut last_start_thr = vad_thr;
        let mut frame_idx = 0usize;
        let mut music_on = false;

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
                if crate::local_ai::is_ringing() {
                    crate::local_ai::stop_ringing();
                    continue;
                }
                if force_active {
                    println!("[atriai] funcbutton pressed again -> cancelled -> clock");
                    speaking = false;
                    buf.clear();
                    utt_rms.clear();
                    empty_followups = 0;
                    set_vad_visual("idle", false, "default");
                    break;
                }
                force_active = true;
                mode = PipelineMode::ActiveCommand;
                vad.wait_quiet_frames = 0;
                empty_followups = 0;
                speaking = false;
                speech_hits = 0;
                silence_ms = 0.0;
                wait_speech_ms = 0.0;
                buf.clear();
                utt_rms.clear();
                set_vad_visual("vad", false, "default");
                println!("[atriai] funcbutton pressed -> ActiveCommand (7s timeout)");
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
                vad.warmup(rms);
                if pre_roll.len() == 8 {
                    pre_roll.pop_front();
                }
                pre_roll.push_back((chunk, rms));
                continue;
            }
            if frame_idx % 8 == 0 {
                music_on = playback_running();
            }
            frame_idx = frame_idx.wrapping_add(1);
            let eff_thr = if music_on { vad_thr * 1.05 } else { vad_thr };
            let (start_thr, keep_thr, max_utt_ms, end_silence_ms) =
                vad.thresholds(mode, eff_thr, cfg.silence_ms);
            last_start_thr = start_thr;

            if !speaking {
                if vad.wait_quiet_frames > 0 && !force_active {
                    vad.update_noise(rms);
                    if rms < keep_thr {
                        vad.wait_quiet_frames -= 1;
                    } else {
                        vad.wait_quiet_frames = 10;
                    }
                    if pre_roll.len() == 8 {
                        pre_roll.pop_front();
                    }
                    pre_roll.push_back((chunk, rms));
                    continue;
                }
                if rms >= start_thr {
                    speech_hits += 1;
                    if speech_hits >= 2 {
                        speaking = true;
                        utt_rms.clear();
                        utt_peak = pre_roll.iter().map(|(_, r)| *r).fold(rms, f64::max);
                        for (p, r) in pre_roll.drain(..) {
                            buf.extend_from_slice(&p);
                            utt_rms.push(r);
                        }
                        buf.extend_from_slice(&chunk);
                        utt_rms.push(rms);
                        silence_ms = 0.0;
                        if force_active || !wake_required {
                            set_vad_visual("vad", false, "default");
                        }
                        continue;
                    }
                } else {
                    vad.update_noise(rms);
                    speech_hits = speech_hits.saturating_sub(1);
                    if force_active {
                        wait_speech_ms += frame_ms;
                        if wait_speech_ms >= 7000.0 {
                            println!("[atriai] active listening timeout (7s without speech) -> clock");
                            break;
                        }
                    }
                }
                if pre_roll.len() == 8 {
                    pre_roll.pop_front();
                }
                pre_roll.push_back((chunk, rms));
            } else {
                speech_hits = 0;
                buf.extend_from_slice(&chunk);
                utt_rms.push(rms);
                if rms > utt_peak {
                    utt_peak = rms;
                }
                let eff_keep_thr = keep_thr.max((utt_peak * 0.22).min(680.0));
                if rms >= eff_keep_thr {
                    silence_ms = 0.0;
                } else {
                    silence_ms += frame_ms;
                }
                let utt_ms = (buf.len() as f64) / ((SR * 2) as f64) * 1000.0;
                let eff_end_silence_ms = match mode {
                    PipelineMode::WakeSpotter => {
                        if utt_ms <= 1100.0 { 260.0 } else { 320.0 }
                    }
                    PipelineMode::ActiveCommand => {
                        if utt_ms <= 950.0 { 280.0 } else { end_silence_ms.min(400.0) }
                    }
                };
                if silence_ms >= eff_end_silence_ms {
                    if buf.len() >= min_bytes {
                        break;
                    }
                    speaking = false;
                    utt_peak = 0.0;
                    buf.clear();
                    utt_rms.clear();
                    silence_ms = 0.0;
                } else if utt_ms >= max_utt_ms {
                    hit_max_utt = true;
                    break;
                }
            }
        }
        if broken_stream {
            stop_capture(&mut proc_child);
            thread::sleep(Duration::from_secs(1));
            continue;
        }
        if speaking && buf.len() >= min_bytes && !is_mic_muted() {
            let peak_rms = utt_rms.iter().copied().fold(0.0f64, f64::max);
            let mean_rms = utt_rms.iter().sum::<f64>() / (utt_rms.len().max(1) as f64);
            if !is_valid_speech(&utt_rms, vad.noise_rms, last_start_thr, music_on) {
                vad.adapt_rejected(mean_rms);
                if mode == PipelineMode::WakeSpotter && hit_max_utt {
                    vad.wait_quiet_frames = 10;
                }
                if force_active {
                    set_vad_visual("idle", false, "default");
                    empty_followups = 0;
                }
                println!(
                    "[vad] rejected flat/noise: peak={peak_rms:.0} mean={mean_rms:.0} noise={:.0} thr={last_start_thr:.0}",
                    vad.noise_rms
                );
                continue;
            }
            let dur_ms = (buf.len() * 1000) / (SR * 2);
            println!(
                "[vad] speech segment captured: dur_ms={dur_ms} peak_rms={peak_rms:.0} noise_rms={:.0} mode={}",
                vad.noise_rms,
                mode.as_str()
            );
            write_status("thinking", "", "", "");
            if force_active || !wake_required {
                set_vad_visual("thinking", false, "default");
            }
            let ww = if force_active { "" } else { ww_cfg.as_str() };
            match send_phrase_to_server(&cfg.server, &buf, ww, force_active) {
                Ok((asr, ans, stay_active)) => {
                    let drain_ms = if stay_active && ans.is_empty() {
                        30
                    } else if stay_active || !ans.is_empty() {
                        60
                    } else {
                        40
                    };
                    drain_capture_pipe(&mut proc_child, drain_ms, read_bytes);
                    if stay_active && (WAKE_TRIGGERED.swap(false, Ordering::SeqCst) || !ans.trim().is_empty()) {
                        pend_force_active = true;
                        empty_followups = 0;
                    } else if stay_active && force_active && asr.is_empty() && empty_followups < 1 {
                        pend_force_active = true;
                        empty_followups += 1;
                    } else {
                        WAKE_TRIGGERED.store(false, Ordering::SeqCst);
                        set_vad_visual("idle", false, "default");
                        empty_followups = 0;
                        if (mode == PipelineMode::WakeSpotter && hit_max_utt) || (!stay_active && !ans.is_empty()) {
                            vad.wait_quiet_frames = 8;
                        }
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
    }
    stop_capture(&mut proc_child);
    0
}

pub fn run(args: &[String]) -> i32 {
    let cmd = args.first().map(String::as_str).unwrap_or("status");
    match cmd {
        "daemon" => run_daemon(),
        "timers" | "alarms" => {
            println!("{}", crate::local_ai::list_status_json().to_string_pretty());
            0
        }
        "local-stt" if args.len() > 1 => {
            if !crate::local_stt::init() {
                eprintln!("Local Vosk STT model or libvosk.so not installed.");
                return 1;
            }
            let Ok(mut raw) = fs::read(&args[1]) else {
                eprintln!("Cannot read {}", args[1]);
                return 1;
            };
            if raw.starts_with(b"RIFF") && raw.len() > 44 {
                match parse_wav(&raw) {
                    Ok(info) => raw = raw[info.data_offset..info.data_offset + info.data_len].to_vec(),
                    Err(e) => {
                        eprintln!("{e}");
                        return 1;
                    }
                }
            }
            match crate::local_stt::transcribe_pcm16(&raw) {
                Ok(t) => {
                    println!("{t}");
                    0
                }
                Err(e) => {
                    eprintln!("{e}");
                    1
                }
            }
        }
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
