use std::fs;
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::{Mutex, OnceLock};
use std::thread;
use std::time::{Duration, Instant};

use crate::atriai::{drain_capture_fd_now, play_chime_interruptible, play_command_chime, set_vad_visual, speak_via_remote_tts, split_trailing_stop};
use crate::json::{obj, Json};
use crate::light::SCREEN_MODE_FILE;
use crate::sys::{atomic_write_bytes, format_local, kill, now_unix_i64};

const TIMERS_FILE: &str = "/var/run/atrios/timers.json";
const ALARMS_FILE: &str = "/var/lib/atrios/alarms.json";

static ALERT_RINGING: AtomicBool = AtomicBool::new(false);
static ALERT_STOP_REQ: AtomicBool = AtomicBool::new(false);
static RING_APLAY_PID: AtomicI32 = AtomicI32::new(0);
static SCHED_STARTED: AtomicBool = AtomicBool::new(false);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TimerItem {
    pub id: u64,
    pub fire_at: i64,
    pub total_secs: u64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AlarmItem {
    pub id: u64,
    pub hour: u8,
    pub minute: u8,
    pub enabled: bool,
    pub last_fired_day: i64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LocalIntent {
    SetTimer { secs: u64 },
    CancelTimers,
    QueryTimers,
    SetAlarm { hour: u8, minute: u8 },
    CancelAlarms,
    QueryAlarms,
    QueryTime,
    QueryDate,
    RingMode(String),
    ScreenMode(String),
}

struct Store {
    next_id: u64,
    timers: Vec<TimerItem>,
    alarms: Vec<AlarmItem>,
}

static STORE: OnceLock<Mutex<Store>> = OnceLock::new();

fn store() -> &'static Mutex<Store> {
    STORE.get_or_init(|| {
        let mut s = Store {
            next_id: 1,
            timers: load_timers_disk(),
            alarms: load_alarms_disk(),
        };
        let max_t = s.timers.iter().map(|t| t.id).max().unwrap_or(0);
        let max_a = s.alarms.iter().map(|a| a.id).max().unwrap_or(0);
        s.next_id = max_t.max(max_a) + 1;
        Mutex::new(s)
    })
}

fn load_timers_disk() -> Vec<TimerItem> {
    let mut out = Vec::new();
    let Ok(txt) = fs::read_to_string(TIMERS_FILE) else { return out };
    let Ok(Json::Arr(arr)) = Json::parse(&txt) else { return out };
    let now = now_unix_i64();
    for item in arr {
        let id = item.get("id").and_then(|v| v.as_i64()).unwrap_or(0).max(0) as u64;
        let fire_at = item.get("fire_at").and_then(|v| v.as_i64()).unwrap_or(0);
        let total_secs = item.get("total_secs").and_then(|v| v.as_i64()).unwrap_or(0).max(0) as u64;
        if fire_at > now - 10 {
            out.push(TimerItem { id, fire_at, total_secs });
        }
    }
    out
}

fn save_timers_disk(timers: &[TimerItem]) {
    let arr: Vec<Json> = timers
        .iter()
        .map(|t| {
            obj(vec![
                ("id", Json::Num(t.id as f64)),
                ("fire_at", Json::Num(t.fire_at as f64)),
                ("total_secs", Json::Num(t.total_secs as f64)),
            ])
        })
        .collect();
    let body = Json::Arr(arr).to_string_pretty();
    let _ = atomic_write_bytes(Path::new(TIMERS_FILE), body.as_bytes(), 0o644, false);
}

fn load_alarms_disk() -> Vec<AlarmItem> {
    let mut out = Vec::new();
    let Ok(txt) = fs::read_to_string(ALARMS_FILE) else { return out };
    let Ok(Json::Arr(arr)) = Json::parse(&txt) else { return out };
    for item in arr {
        let id = item.get("id").and_then(|v| v.as_i64()).unwrap_or(0).max(0) as u64;
        let hour = item.get("hour").and_then(|v| v.as_i64()).unwrap_or(-1);
        let minute = item.get("minute").and_then(|v| v.as_i64()).unwrap_or(-1);
        let enabled = item.get("enabled").and_then(|v| v.as_bool()).unwrap_or(true);
        let last_fired_day = item.get("last_fired_day").and_then(|v| v.as_i64()).unwrap_or(0);
        if (0..=23).contains(&hour) && (0..=59).contains(&minute) {
            out.push(AlarmItem {
                id,
                hour: hour as u8,
                minute: minute as u8,
                enabled,
                last_fired_day,
            });
        }
    }
    out
}

fn save_alarms_disk(alarms: &[AlarmItem]) {
    let arr: Vec<Json> = alarms
        .iter()
        .map(|a| {
            obj(vec![
                ("id", Json::Num(a.id as f64)),
                ("hour", Json::Num(a.hour as f64)),
                ("minute", Json::Num(a.minute as f64)),
                ("enabled", Json::Bool(a.enabled)),
                ("last_fired_day", Json::Num(a.last_fired_day as f64)),
            ])
        })
        .collect();
    let body = Json::Arr(arr).to_string_pretty();
    let _ = atomic_write_bytes(Path::new(ALARMS_FILE), body.as_bytes(), 0o644, true);
}

pub fn is_ringing() -> bool {
    ALERT_RINGING.load(Ordering::SeqCst)
}

pub fn stop_ringing() {
    let was_ringing = ALERT_RINGING.load(Ordering::SeqCst);
    ALERT_STOP_REQ.store(true, Ordering::SeqCst);
    let pid = RING_APLAY_PID.swap(0, Ordering::SeqCst);
    if pid > 0 {
        unsafe {
            let _ = kill(pid, 9);
        }
    }
    // Clear any timers that already expired so a back-to-back timer doesn't re-ring immediately
    let now = now_unix_i64();
    if let Ok(mut s) = store().lock() {
        let before = s.timers.len();
        s.timers.retain(|t| t.fire_at > now + 1);
        if s.timers.len() != before {
            save_timers_disk(&s.timers);
        }
    }
    if was_ringing {
        println!("[local-ai] alert stop requested (killed aplay pid={pid})");
    }
}

pub fn format_duration_short(secs: u64) -> String {
    let h = secs / 3600;
    let m = (secs % 3600) / 60;
    let s = secs % 60;
    if h > 0 {
        format!("{h}:{m:02}:{s:02}")
    } else {
        format!("{m:02}:{s:02}")
    }
}

fn ru_num_token(w: &str) -> Option<(u64, bool)> {
    if let Ok(n) = w.parse::<u64>() {
        return Some((n, false));
    }
    match w {
        "ноль" | "нуль" => Some((0, false)),
        "один" | "одна" | "одну" | "одного" | "первого" | "час" => Some((1, false)),
        "два" | "две" | "двух" | "второго" => Some((2, false)),
        "три" | "трех" | "третьего" => Some((3, false)),
        "четыре" | "четырех" | "четвертого" => Some((4, false)),
        "пять" | "пяти" | "пятого" => Some((5, false)),
        "шесть" | "шести" | "шестого" => Some((6, false)),
        "семь" | "семи" | "седьмого" => Some((7, false)),
        "восемь" | "восьми" | "восьмого" => Some((8, false)),
        "девять" | "девяти" | "девятого" => Some((9, false)),
        "десять" | "десяти" | "десятого" => Some((10, false)),
        "одиннадцать" | "одиннадцати" | "одиннадцатого" => Some((11, false)),
        "двенадцать" | "двенадцати" | "двенадцатого" => Some((12, false)),
        "тринадцать" => Some((13, false)),
        "четырнадцать" => Some((14, false)),
        "пятнадцать" => Some((15, false)),
        "шестнадцать" => Some((16, false)),
        "семнадцать" => Some((17, false)),
        "восемнадцать" => Some((18, false)),
        "девятнадцать" => Some((19, false)),
        "двадцать" => Some((20, true)),
        "тридцать" => Some((30, true)),
        "сорок" => Some((40, true)),
        "пятьдесят" => Some((50, true)),
        "шестьдесят" => Some((60, true)),
        "семьдесят" => Some((70, true)),
        "восемьдесят" => Some((80, true)),
        "девяносто" => Some((90, true)),
        "сто" => Some((100, false)),
        _ => None,
    }
}

fn consume_number(words: &[&str], idx: usize) -> Option<(u64, usize)> {
    let w = *words.get(idx)?;
    if w == "час" {
        return None;
    }
    let (v, is_ten) = ru_num_token(w)?;
    if is_ten {
        if let Some(&nxt) = words.get(idx + 1) {
            if nxt != "час" {
                if let Some((u, false)) = ru_num_token(nxt) {
                    if (1..=9).contains(&u) {
                        return Some((v + u, idx + 2));
                    }
                }
            }
        }
    }
    Some((v, idx + 1))
}

fn unit_mult(w: &str) -> Option<u64> {
    if matches!(w, "секунда" | "секунду" | "секунды" | "секунд" | "сек" | "с") {
        Some(1)
    } else if matches!(w, "минута" | "минуту" | "минуты" | "минут" | "мин" | "м") {
        Some(60)
    } else if matches!(w, "час" | "часа" | "часов" | "ч") {
        Some(3600)
    } else {
        None
    }
}

pub fn parse_russian_duration_secs(words: &[&str]) -> Option<u64> {
    let mut total = 0u64;
    let mut i = 0usize;
    let mut saw_unit = false;
    while i < words.len() {
        let w = words[i];
        if matches!(w, "полминуты" | "полминутки") {
            total += 30;
            saw_unit = true;
            i += 1;
            continue;
        }
        if w == "полчаса" {
            total += 1800;
            saw_unit = true;
            i += 1;
            continue;
        }
        if matches!(w, "полтора" | "полторы") {
            if let Some(&u) = words.get(i + 1) {
                if let Some(m) = unit_mult(u) {
                    total += m + m / 2;
                    saw_unit = true;
                    i += 2;
                    continue;
                }
            }
        }
        if let Some((num, next_i)) = consume_number(words, i) {
            if let Some(&u) = words.get(next_i) {
                if let Some(m) = unit_mult(u) {
                    total += num.saturating_mul(m);
                    saw_unit = true;
                    i = next_i + 1;
                    continue;
                }
            }
        }
        if let Some(m) = unit_mult(w) {
            total += m;
            saw_unit = true;
            i += 1;
            continue;
        }
        i += 1;
    }
    if saw_unit && total > 0 {
        return Some(total.clamp(1, 86400));
    }
    // Fallback: "таймер на 5" -> 5 minutes
    for idx in 0..words.len() {
        if let Some((num, _)) = consume_number(words, idx) {
            if num > 0 {
                return Some((num * 60).clamp(1, 86400));
            }
        }
    }
    None
}

pub fn parse_russian_alarm_time(words: &[&str]) -> Option<(u8, u8)> {
    // 1. Direct "HH:MM" token
    for &w in words {
        if let Some((h_s, m_s)) = w.split_once(':') {
            if let (Ok(h), Ok(m)) = (h_s.parse::<u8>(), m_s.parse::<u8>()) {
                if h <= 23 && m <= 59 {
                    return Some((h, m));
                }
            }
        }
    }

    // 2. Relative "через <duration>"
    if let Some(pos) = words.iter().position(|&w| w == "через") {
        if let Some(secs) = parse_russian_duration_secs(&words[pos + 1..]) {
            let target = now_unix_i64() + secs as i64;
            let hm = format_local("%H:%M", Some(target));
            if let Some((h_s, m_s)) = hm.split_once(':') {
                if let (Ok(h), Ok(m)) = (h_s.parse::<u8>(), m_s.parse::<u8>()) {
                    return Some((h, m));
                }
            }
        }
    }

    let is_evening = words.iter().any(|&w| matches!(w, "вечера" | "вечером" | "дня" | "днем"));
    let is_morning = words.iter().any(|&w| matches!(w, "утра" | "утром"));
    let is_night = words.iter().any(|&w| matches!(w, "ночи" | "ночью"));

    let adjust_hour = |mut h: u64| -> Option<u8> {
        if h > 23 {
            return None;
        }
        if is_evening && (1..=11).contains(&h) {
            h += 12;
        } else if is_night && h == 12 {
            h = 0;
        } else if is_morning && h == 12 {
            h = 0;
        }
        Some(h as u8)
    };

    // 3. "полвосьмого" / "в половине восьмого"
    for (idx, &w) in words.iter().enumerate() {
        let half_hour = match w {
            "полпервого" => Some(0u64),
            "полвторого" => Some(1),
            "полтретьего" => Some(2),
            "полчетвертого" => Some(3),
            "полпятого" => Some(4),
            "полшестого" => Some(5),
            "полседьмого" => Some(6),
            "полвосьмого" => Some(7),
            "полдевятого" => Some(8),
            "полдесятого" => Some(9),
            "полодиннадцатого" => Some(10),
            "полдвенадцатого" => Some(11),
            _ => None,
        };
        if let Some(h) = half_hour {
            return adjust_hour(h).map(|hh| (hh, 30));
        }
        if matches!(w, "половине" | "половину" | "пол") {
            if let Some(&ord) = words.get(idx + 1) {
                if let Some((n, _)) = ru_num_token(ord) {
                    if (1..=12).contains(&n) {
                        let h = if n == 1 { 0 } else { n - 1 };
                        return adjust_hour(h).map(|hh| (hh, 30));
                    }
                }
            }
        }
    }

    // 4. Extract up to 2 numbers after "на" / "в" (or anywhere)
    let start_idx = words
        .iter()
        .position(|&w| matches!(w, "на" | "в" | "во"))
        .map(|p| p + 1)
        .unwrap_or(0);
    let mut nums: Vec<u64> = Vec::new();
    let mut i = start_idx;
    while i < words.len() {
        if let Some((n, nxt)) = consume_number(words, i) {
            nums.push(n);
            i = nxt;
            continue;
        }
        i += 1;
    }
    if nums.is_empty() && start_idx > 0 {
        let mut i = 0usize;
        while i < words.len() {
            if let Some((n, nxt)) = consume_number(words, i) {
                nums.push(n);
                i = nxt;
                continue;
            }
            i += 1;
        }
    }
    if nums.is_empty() {
        return None;
    }
    let h = adjust_hour(nums[0])?;
    let m = if nums.len() >= 2 && nums[1] <= 59 {
        nums[1] as u8
    } else {
        0
    };
    Some((h, m))
}

pub fn parse_local_intent(text: &str, wake_word: &str) -> Option<LocalIntent> {
    let (clean, _) = split_trailing_stop(text, wake_word);
    let words: Vec<&str> = clean.split_whitespace().collect();
    if words.is_empty() {
        return None;
    }

    let has_timer = words.iter().any(|&w| {
        w.starts_with("таймер") || matches!(w, "засеки" | "засечь" | "напомни")
    });
    let has_alarm = words.iter().any(|&w| {
        w.starts_with("будильник") || matches!(w, "разбуди" | "подними")
    });
    let has_cancel = words.iter().any(|&w| {
        matches!(
            w,
            "отмени" | "отменить" | "удали" | "удалить" | "выключи" | "выключить"
                | "убери" | "убрать" | "сбрось" | "сбросить" | "очисти" | "останови"
        )
    });
    let has_query = words.iter().any(|&w| {
        matches!(w, "сколько" | "какие" | "какой" | "статус" | "покажи" | "осталось" | "во")
    });

    if has_timer {
        if has_cancel {
            return Some(LocalIntent::CancelTimers);
        }
        if has_query && parse_russian_duration_secs(&words).is_none() {
            return Some(LocalIntent::QueryTimers);
        }
        if let Some(secs) = parse_russian_duration_secs(&words) {
            return Some(LocalIntent::SetTimer { secs });
        }
        return Some(LocalIntent::QueryTimers);
    }

    if words.iter().any(|&w| w == "осталось") && words.iter().any(|&w| w == "сколько") {
        return Some(LocalIntent::QueryTimers);
    }

    if has_alarm {
        if has_cancel {
            return Some(LocalIntent::CancelAlarms);
        }
        if has_query && parse_russian_alarm_time(&words).is_none() {
            return Some(LocalIntent::QueryAlarms);
        }
        if let Some((hour, minute)) = parse_russian_alarm_time(&words) {
            return Some(LocalIntent::SetAlarm { hour, minute });
        }
        return Some(LocalIntent::QueryAlarms);
    }

    // Time & date queries
    let joined = words.join(" ");
    if matches!(
        joined.as_str(),
        "сколько времени" | "который час" | "сколько сейчас времени" | "какое время" | "время"
    ) || (words.contains(&"сколько") && words.contains(&"времени"))
        || (words.contains(&"который") && words.contains(&"час"))
    {
        return Some(LocalIntent::QueryTime);
    }
    if (words.contains(&"какое") && words.contains(&"число"))
        || (words.contains(&"какая") && words.contains(&"дата"))
        || joined == "дата"
    {
        return Some(LocalIntent::QueryDate);
    }

    // LED ring lighting control
    let has_light = words.iter().any(|&w| {
        w.starts_with("подсветк") || w.starts_with("кольц") || matches!(w, "лампу" | "огоньки")
    });
    if has_light {
        if has_cancel {
            return Some(LocalIntent::RingMode("off".into()));
        }
        if words.iter().any(|&w| w.starts_with("радуг")) {
            return Some(LocalIntent::RingMode("rainbow".into()));
        }
        if words.iter().any(|&w| w.starts_with("син") || w.starts_with("голуб")) {
            return Some(LocalIntent::RingMode("blue".into()));
        }
        if words.iter().any(|&w| w.starts_with("зелен")) {
            return Some(LocalIntent::RingMode("green".into()));
        }
        if words.iter().any(|&w| w.starts_with("красн")) {
            return Some(LocalIntent::RingMode("red".into()));
        }
        if words.iter().any(|&w| w.starts_with("желт") || w.starts_with("оранж") || w.starts_with("янтар")) {
            return Some(LocalIntent::RingMode("amber".into()));
        }
        return Some(LocalIntent::RingMode("rainbow".into()));
    }

    // Screen animations
    if words.iter().any(|&w| matches!(w, "кота" | "кот" | "котика" | "бойкиссер" | "киссер")) {
        return Some(LocalIntent::ScreenMode("kisser".into()));
    }
    if words.contains(&"покажи") && words.contains(&"часы") {
        return Some(LocalIntent::ScreenMode("clock".into()));
    }

    None
}

fn ru_plural<'a>(n: u64, one: &'a str, few: &'a str, many: &'a str) -> &'a str {
    let m100 = n % 100;
    let m10 = n % 10;
    if (11..=14).contains(&m100) {
        many
    } else if m10 == 1 {
        one
    } else if (2..=4).contains(&m10) {
        few
    } else {
        many
    }
}

fn format_duration_ru(secs: u64) -> String {
    let h = secs / 3600;
    let m = (secs % 3600) / 60;
    let s = secs % 60;
    let mut parts = Vec::new();
    if h > 0 {
        parts.push(format!("{h} {}", ru_plural(h, "час", "часа", "часов")));
    }
    if m > 0 {
        parts.push(format!("{m} {}", ru_plural(m, "минуту", "минуты", "минут")));
    }
    if s > 0 || parts.is_empty() {
        parts.push(format!("{s} {}", ru_plural(s, "секунду", "секунды", "секунд")));
    }
    parts.join(" ")
}

fn format_time_ru(hour: u8, minute: u8) -> String {
    let hs = format!("{hour} {}", ru_plural(hour as u64, "час", "часа", "часов"));
    if minute == 0 {
        format!("{hs} ровно")
    } else {
        format!("{hs} {minute} {}", ru_plural(minute as u64, "минута", "минуты", "минут"))
    }
}

fn show_ticker_briefly(text: &str) {
    let _ = fs::create_dir_all("/var/run/atrios");
    let _ = fs::write(SCREEN_MODE_FILE, "ticker");
    let _ = Command::new("pkill")
        .args(["-f", "/usr/lib/atrios/light screen"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
    let _ = Command::new("/usr/lib/atrios/light")
        .args(["screen", "ticker", text])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
    let _ = fs::write(SCREEN_MODE_FILE, "clock");
}

pub fn execute_local_intent(intent: LocalIntent) {
    match intent {
        LocalIntent::SetTimer { secs } => {
            let now = now_unix_i64();
            let fire_at = now + secs as i64;
            {
                let mut s = store().lock().unwrap();
                let id = s.next_id;
                s.next_id += 1;
                s.timers.push(TimerItem {
                    id,
                    fire_at,
                    total_secs: secs,
                });
                save_timers_disk(&s.timers);
            }
            let lbl = format!("TMR {}", format_duration_short(secs));
            println!("[local-ai] timer set: {secs}s ({lbl})");
            let spoken = format!("Поставила таймер на {}!", format_duration_ru(secs));
            if !speak_via_remote_tts(&spoken, "happy", false) {
                play_command_chime("timer_set");
                show_ticker_briefly(&lbl);
            }
        }
        LocalIntent::CancelTimers => {
            {
                let mut s = store().lock().unwrap();
                s.timers.clear();
                save_timers_disk(&s.timers);
            }
            stop_ringing();
            println!("[local-ai] all timers cancelled");
            if !speak_via_remote_tts("Хорошо, все таймеры отменены.", "neutral", false) {
                play_command_chime("cancel");
                show_ticker_briefly("TMR OFF");
            }
        }
        LocalIntent::QueryTimers => {
            let now = now_unix_i64();
            let rem = {
                let s = store().lock().unwrap();
                s.timers
                    .iter()
                    .filter(|t| t.fire_at > now)
                    .map(|t| (t.fire_at - now) as u64)
                    .min()
            };
            let (lbl, spoken) = match rem {
                Some(r) => (
                    format!("TMR {}", format_duration_short(r)),
                    format!("До конца таймера осталось {}.", format_duration_ru(r)),
                ),
                None => ("NO TMR".into(), "Активных таймеров сейчас нет.".into()),
            };
            println!("[local-ai] query timers -> {lbl}");
            if !speak_via_remote_tts(&spoken, "neutral", false) {
                play_command_chime("tick");
                show_ticker_briefly(&lbl);
            }
        }
        LocalIntent::SetAlarm { hour, minute } => {
            {
                let mut s = store().lock().unwrap();
                let id = s.next_id;
                s.next_id += 1;
                s.alarms.retain(|a| !(a.hour == hour && a.minute == minute));
                s.alarms.push(AlarmItem {
                    id,
                    hour,
                    minute,
                    enabled: true,
                    last_fired_day: 0,
                });
                save_alarms_disk(&s.alarms);
            }
            let lbl = format!("ALM {:02}:{:02}", hour, minute);
            println!("[local-ai] alarm set: {lbl}");
            let spoken = format!("Будильник на {} установлен!", format_time_ru(hour, minute));
            if !speak_via_remote_tts(&spoken, "happy", false) {
                play_command_chime("alarm_set");
                show_ticker_briefly(&lbl);
            }
        }
        LocalIntent::CancelAlarms => {
            {
                let mut s = store().lock().unwrap();
                s.alarms.clear();
                save_alarms_disk(&s.alarms);
            }
            stop_ringing();
            println!("[local-ai] all alarms cancelled");
            if !speak_via_remote_tts("Хорошо, будильники отменены.", "neutral", false) {
                play_command_chime("cancel");
                show_ticker_briefly("ALM OFF");
            }
        }
        LocalIntent::QueryAlarms => {
            let (lbl, spoken) = {
                let s = store().lock().unwrap();
                if let Some(a) = s.alarms.iter().find(|a| a.enabled) {
                    (
                        format!("ALM {:02}:{:02}", a.hour, a.minute),
                        format!("Будильник стоит на {}.", format_time_ru(a.hour, a.minute)),
                    )
                } else {
                    ("NO ALM".into(), "Будильников пока нет.".into())
                }
            };
            println!("[local-ai] query alarms -> {lbl}");
            if !speak_via_remote_tts(&spoken, "neutral", false) {
                play_command_chime("tick");
                show_ticker_briefly(&lbl);
            }
        }
        LocalIntent::QueryTime => {
            let hm = format_local("%H:%M", None);
            println!("[local-ai] time -> {hm}");
            let spoken = if let Some((hs, ms)) = hm.split_once(':') {
                if let (Ok(h), Ok(m)) = (hs.parse::<u8>(), ms.parse::<u8>()) {
                    format!("Сейчас {}.", format_time_ru(h, m))
                } else {
                    format!("Сейчас {hm}.")
                }
            } else {
                format!("Сейчас {hm}.")
            };
            if !speak_via_remote_tts(&spoken, "neutral", false) {
                play_command_chime("tick");
                show_ticker_briefly(&hm);
            }
        }
        LocalIntent::QueryDate => {
            let dt = format_local("%d.%m", None);
            println!("[local-ai] date -> {dt}");
            play_command_chime("tick");
            show_ticker_briefly(&dt);
        }
        LocalIntent::RingMode(mode) => {
            println!("[local-ai] ring mode -> {mode}");
            let _ = fs::create_dir_all("/var/run/atrios");
            let _ = fs::write("/var/run/atrios/ring_mode", &mode);
            let _ = Command::new("pkill")
                .args(["-f", "/usr/lib/atrios/light ring"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            let mut cmd = Command::new("/usr/lib/atrios/light");
            cmd.args(["ring", &mode]);
            if mode == "amber" {
                cmd.arg("0");
            }
            let _ = cmd.stdout(Stdio::null()).stderr(Stdio::null()).spawn();
            play_command_chime("ring_mode");
        }
        LocalIntent::ScreenMode(mode) => {
            println!("[local-ai] screen mode -> {mode}");
            play_command_chime("tick");
            if mode == "kisser" {
                let _ = fs::write(SCREEN_MODE_FILE, "kisser");
                let _ = Command::new("pkill")
                    .args(["-f", "/usr/lib/atrios/light screen"])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .status();
                if let Ok(mut c) = Command::new("/usr/lib/atrios/light")
                    .args(["screen", "kisser", "animate"])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .spawn()
                {
                    thread::sleep(Duration::from_secs(4));
                    let _ = c.kill();
                    let _ = c.wait();
                }
                let _ = fs::write(SCREEN_MODE_FILE, "clock");
            } else {
                set_vad_visual("idle", true, "default");
            }
        }
    }
}

pub fn list_status_json() -> Json {
    let s = store().lock().unwrap();
    let now = now_unix_i64();
    let timers: Vec<Json> = s
        .timers
        .iter()
        .map(|t| {
            obj(vec![
                ("id", Json::Num(t.id as f64)),
                ("remaining_sec", Json::Num((t.fire_at - now).max(0) as f64)),
                ("total_secs", Json::Num(t.total_secs as f64)),
            ])
        })
        .collect();
    let alarms: Vec<Json> = s
        .alarms
        .iter()
        .map(|a| {
            obj(vec![
                ("id", Json::Num(a.id as f64)),
                ("time", Json::Str(format!("{:02}:{:02}", a.hour, a.minute))),
                ("enabled", Json::Bool(a.enabled)),
            ])
        })
        .collect();
    obj(vec![("timers", Json::Arr(timers)), ("alarms", Json::Arr(alarms))])
}

fn ring_alert_loop(kind: &str) {
    if ALERT_RINGING.swap(true, Ordering::SeqCst) {
        return;
    }
    ALERT_STOP_REQ.store(false, Ordering::SeqCst);
    println!("[local-ai] RINGING {kind} (say 'стоп' or press button to stop)");
    let _ = fs::create_dir_all("/var/run/atrios");
    let _ = fs::write(SCREEN_MODE_FILE, "timer");
    let _ = Command::new("pkill")
        .args(["-f", "/usr/lib/atrios/light screen"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
    let mut screen_child = Command::new("/usr/lib/atrios/light")
        .args(["screen", "eyes", "timer"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .ok();
    let _ = Command::new("pkill")
        .args(["-f", "/usr/lib/atrios/light ring"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
    let mut ring_child = Command::new("/usr/lib/atrios/light")
        .args(["ring", "amber", "60"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .ok();

    let start = Instant::now();
    let chime_name = if kind == "alarm" { "alarm_ring" } else { "timer_ring" };
    let ring_phrase = if kind == "alarm" {
        "Доброе утро! Сработал будильник!"
    } else {
        "Время вышло! Таймер сработал!"
    };
    let mut ring_cycle = 0u32;
    while !ALERT_STOP_REQ.load(Ordering::SeqCst) && start.elapsed() < Duration::from_secs(60) {
        if ring_cycle % 3 == 0 {
            let _ = speak_via_remote_tts(ring_phrase, "happy", true);
        }
        ring_cycle += 1;
        if ALERT_STOP_REQ.load(Ordering::SeqCst) {
            break;
        }
        play_chime_interruptible(chime_name, &ALERT_STOP_REQ, &RING_APLAY_PID);
        for _ in 0..16 {
            if ALERT_STOP_REQ.load(Ordering::SeqCst) {
                break;
            }
            thread::sleep(Duration::from_millis(45));
        }
    }
    RING_APLAY_PID.store(0, Ordering::SeqCst);

    if let Some(mut c) = screen_child.take() {
        let _ = c.kill();
        let _ = c.wait();
    }
    if let Some(mut c) = ring_child.take() {
        let _ = c.kill();
        let _ = c.wait();
    }
    // Force-restore idle clock display and turn off LED ring so the timer bell never freezes on screen.
    set_vad_visual("idle", true, "default");
    drain_capture_fd_now();
    ALERT_RINGING.store(false, Ordering::SeqCst);
    ALERT_STOP_REQ.store(false, Ordering::SeqCst);
    println!("[local-ai] {kind} alert stopped -> clock (forced idle visual)");
}

pub fn start_scheduler() {
    if SCHED_STARTED.swap(true, Ordering::SeqCst) {
        return;
    }
    let _ = store();
    thread::spawn(|| loop {
        thread::sleep(Duration::from_millis(500));
        if ALERT_RINGING.load(Ordering::SeqCst) {
            continue;
        }
        let now = now_unix_i64();
        let mut fired_kind: Option<&'static str> = None;
        {
            let mut s = store().lock().unwrap();
            let before = s.timers.len();
            s.timers.retain(|t| t.fire_at > now);
            if s.timers.len() != before {
                save_timers_disk(&s.timers);
                fired_kind = Some("timer");
            }
            if fired_kind.is_none() {
                let hm = format_local("%H:%M", Some(now));
                let day_num = now / 86400;
                if let Some((hs, ms)) = hm.split_once(':') {
                    if let (Ok(ch), Ok(cm)) = (hs.parse::<u8>(), ms.parse::<u8>()) {
                        let mut changed = false;
                        for a in &mut s.alarms {
                            if a.enabled && a.hour == ch && a.minute == cm && a.last_fired_day != day_num {
                                a.last_fired_day = day_num;
                                changed = true;
                                fired_kind = Some("alarm");
                                break;
                            }
                        }
                        if changed {
                            save_alarms_disk(&s.alarms);
                        }
                    }
                }
            }
        }
        if let Some(kind) = fired_kind {
            ring_alert_loop(kind);
        }
    });
}
