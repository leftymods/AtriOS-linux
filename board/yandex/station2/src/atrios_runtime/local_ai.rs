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
const TIMER_DISPLAY_FILE: &str = "/var/run/atrios/timer_display.json";
const TIMER_DISPLAY_DEFAULT_SECS: i64 = 15;
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
    ShowTimer { target_secs: Option<u64>, index: Option<usize>, pin: bool },
    HideTimerDisplay,
    CancelTimers { target_secs: Option<u64>, index: Option<usize>, all: bool },
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

fn set_timer_display(timer_id: u64, pinned: bool) {
    let until = now_unix_i64() + TIMER_DISPLAY_DEFAULT_SECS;
    let j = obj(vec![
        ("timer_id", Json::Num(timer_id as f64)),
        ("until", Json::Num(until as f64)),
        ("pinned", Json::Bool(pinned)),
    ]);
    let _ = fs::create_dir_all("/var/run/atrios");
    let _ = atomic_write_bytes(
        Path::new(TIMER_DISPLAY_FILE),
        j.to_string_compact().as_bytes(),
        0o644,
        false,
    );
}

fn clear_timer_display() {
    let _ = fs::remove_file(TIMER_DISPLAY_FILE);
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

fn strip_inline_wake_tokens<'a>(raw: &[&'a str]) -> Vec<&'a str> {
    let mut out = Vec::with_capacity(raw.len());
    let mut i = 0usize;
    while i < raw.len() {
        if i + 1 < raw.len()
            && matches!(raw[i], "а" | "о" | "от" | "на" | "э")
            && raw[i + 1] == "три"
            && !(i > 0 && matches!(raw[i - 1], "на" | "в" | "во" | "через"))
        {
            i += 2;
            continue;
        }
        if matches!(
            raw[i],
            "атри" | "отри" | "арти" | "атриум" | "натрий" | "астры" | "адрес" | "армия" | "актриса" | "атрибут" | "atri"
        ) {
            i += 1;
            continue;
        }
        out.push(raw[i]);
        i += 1;
    }
    out
}

pub fn parse_local_intent(text: &str, wake_word: &str) -> Option<LocalIntent> {
    let (clean, _) = split_trailing_stop(text, wake_word);
    let raw_words: Vec<&str> = clean.split_whitespace().collect();
    let words = strip_inline_wake_tokens(&raw_words);
    if words.is_empty() {
        return None;
    }

    let timer_pos = words.iter().position(|&w| {
        w.starts_with("таймер") || matches!(w, "засеки" | "засечь" | "напомни")
    });
    let alarm_pos = words.iter().position(|&w| {
        w.starts_with("будильник") || matches!(w, "разбуди" | "подними")
    });
    let has_timer = timer_pos.is_some();
    let has_alarm = alarm_pos.is_some();
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

    let has_show = words.iter().any(|&w| {
        matches!(w, "покажи" | "показать" | "выведи" | "открой" | "отобрази")
    });
    let has_pin = words.iter().any(|&w| {
        matches!(w, "оставь" | "оставить" | "закрепи" | "закрепить" | "убирай" | "держи" | "зафиксируй")
    });
    let has_screen_word = words.iter().any(|&w| {
        w.starts_with("экран") || w.starts_with("диспле")
    });
    let has_set_verb = words.iter().any(|&w| {
        matches!(w, "поставь" | "поставить" | "установи" | "установить" | "заведи" | "завести" | "засеки" | "засечь" | "добавь" | "создай" | "напомни")
    });
    let ordinal_idx = if words.iter().any(|&w| w.starts_with("перв")) {
        Some(0usize)
    } else if words.iter().any(|&w| w.starts_with("втор")) {
        Some(1usize)
    } else if words.iter().any(|&w| w.starts_with("трет")) {
        Some(2usize)
    } else {
        None
    };

    if has_timer {
        if has_cancel && has_screen_word {
            return Some(LocalIntent::HideTimerDisplay);
        }
        // Only parse duration from words after the timer keyword so stray wake words ("три") never leak in
        let tail = timer_pos.map(|p| &words[p + 1..]).unwrap_or(&words[..]);
        let dur = parse_russian_duration_secs(tail).or_else(|| parse_russian_duration_secs(&words));
        if has_cancel {
            let explicit_all = words.iter().any(|&w| matches!(w, "все" | "всё" | "таймеры" | "таймеров"));
            let cancel_all = explicit_all && dur.is_none() && ordinal_idx.is_none();
            return Some(LocalIntent::CancelTimers {
                target_secs: if cancel_all { None } else { dur },
                index: if cancel_all { None } else { ordinal_idx },
                all: cancel_all,
            });
        }
        if (has_show || has_pin) && !has_set_verb {
            return Some(LocalIntent::ShowTimer {
                target_secs: dur,
                index: ordinal_idx,
                pin: has_pin,
            });
        }
        if has_query && dur.is_none() {
            return Some(LocalIntent::QueryTimers);
        }
        if let Some(secs) = dur {
            return Some(LocalIntent::SetTimer { secs });
        }
        return Some(LocalIntent::QueryTimers);
    }

    // "оставь на экране" / "не убирай с экрана" without repeating the word "таймер"
    if has_pin && has_screen_word {
        return Some(LocalIntent::ShowTimer {
            target_secs: None,
            index: ordinal_idx,
            pin: true,
        });
    }

    if words.iter().any(|&w| w == "осталось") && words.iter().any(|&w| w == "сколько") {
        return Some(LocalIntent::QueryTimers);
    }

    if has_alarm {
        if has_cancel {
            return Some(LocalIntent::CancelAlarms);
        }
        // Only parse alarm time from words after the alarm keyword so stray wake words ("три") never leak in
        let tail = alarm_pos.map(|p| &words[p + 1..]).unwrap_or(&words[..]);
        let tm = parse_russian_alarm_time(tail).or_else(|| parse_russian_alarm_time(&words));
        if has_query && tm.is_none() {
            return Some(LocalIntent::QueryAlarms);
        }
        if let Some((hour, minute)) = tm {
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

fn show_timer_popup_async(cmd: &'static str, arg: Option<String>) {
    thread::spawn(move || {
        let _ = fs::create_dir_all("/var/run/atrios");
        let _ = fs::write(SCREEN_MODE_FILE, "timer_popup");
        let _ = Command::new("pkill")
            .args(["-f", "/usr/lib/atrios/light screen"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        let mut c = Command::new("/usr/lib/atrios/light");
        c.args(["screen", cmd]);
        if let Some(a) = arg {
            c.arg(a);
        }
        let _ = c.stdout(Stdio::null()).stderr(Stdio::null()).status();
        if fs::read_to_string(SCREEN_MODE_FILE)
            .map(|s| s.trim() == "timer_popup")
            .unwrap_or(false)
        {
            let _ = fs::write(SCREEN_MODE_FILE, "clock");
        }
    });
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
            let new_id = {
                let mut s = store().lock().unwrap();
                let id = s.next_id;
                s.next_id += 1;
                s.timers.push(TimerItem {
                    id,
                    fire_at,
                    total_secs: secs,
                });
                save_timers_disk(&s.timers);
                id
            };
            set_timer_display(new_id, false);
            let lbl = format!("TMR {}", format_duration_short(secs));
            println!("[local-ai] timer set: id={new_id} {secs}s ({lbl}) [auto-hide 15s]");
            show_timer_popup_async("timer_set", Some(secs.to_string()));
            let spoken = format!("Поставила таймер на {}!", format_duration_ru(secs));
            if !speak_via_remote_tts(&spoken, "happy", false) {
                play_command_chime("timer_set");
            }
        }
        LocalIntent::ShowTimer { target_secs, index, pin } => {
            let now = now_unix_i64();
            let mut active: Vec<TimerItem> = {
                let s = store().lock().unwrap();
                s.timers.iter().filter(|t| t.fire_at > now).cloned().collect()
            };
            active.sort_by_key(|t| t.fire_at);
            if active.is_empty() {
                clear_timer_display();
                if !speak_via_remote_tts("Активных таймеров сейчас нет.", "neutral", false) {
                    play_command_chime("tick");
                    show_ticker_briefly("NO TMR");
                }
                return;
            }
            let picked = if let Some(req) = target_secs {
                active
                    .iter()
                    .min_by_key(|t| t.total_secs.abs_diff(req))
                    .cloned()
                    .unwrap()
            } else if let Some(idx) = index {
                active.get(idx).or_else(|| active.first()).cloned().unwrap()
            } else {
                let cur_id = fs::read_to_string(TIMER_DISPLAY_FILE)
                    .ok()
                    .and_then(|raw| Json::parse(&raw).ok())
                    .and_then(|j| j.get("timer_id").and_then(|v| v.as_i64()))
                    .unwrap_or(0) as u64;
                active
                    .iter()
                    .find(|t| t.id == cur_id)
                    .or_else(|| active.first())
                    .cloned()
                    .unwrap()
            };
            let rem = (picked.fire_at - now).max(1) as u64;
            set_timer_display(picked.id, pin);
            println!(
                "[local-ai] show timer id={} total={}s rem={}s pinned={pin}",
                picked.id, picked.total_secs, rem
            );
            show_timer_popup_async("timer_set", Some(rem.to_string()));
            let spoken = if pin {
                format!("Оставила таймер на экране. Осталось {}.", format_duration_ru(rem))
            } else {
                format!("До конца таймера осталось {}.", format_duration_ru(rem))
            };
            if !speak_via_remote_tts(&spoken, "neutral", false) {
                let fallback = format!("До конца таймера осталось {}.", format_duration_ru(rem));
                if !speak_via_remote_tts(&fallback, "neutral", false) {
                    play_command_chime("tick");
                }
            }
        }
        LocalIntent::HideTimerDisplay => {
            clear_timer_display();
            let _ = fs::write(SCREEN_MODE_FILE, "clock");
            println!("[local-ai] timer hidden from display (still running in background)");
            if !speak_via_remote_tts("Готово.", "neutral", false) {
                play_command_chime("tick");
            }
        }
        LocalIntent::CancelTimers { target_secs, index, all } => {
            if all {
                {
                    let mut s = store().lock().unwrap();
                    s.timers.clear();
                    save_timers_disk(&s.timers);
                }
                clear_timer_display();
                stop_ringing();
                println!("[local-ai] all timers cancelled");
                show_timer_popup_async("timer_off", None);
                if !speak_via_remote_tts("Хорошо, все таймеры отменены.", "neutral", false) {
                    play_command_chime("cancel");
                }
            } else {
                let now = now_unix_i64();
                let removed: Option<TimerItem> = {
                    let mut s = store().lock().unwrap();
                    let mut active_indices: Vec<usize> = s
                        .timers
                        .iter()
                        .enumerate()
                        .filter(|(_, t)| t.fire_at > now)
                        .map(|(i, _)| i)
                        .collect();
                    active_indices.sort_by_key(|&i| s.timers[i].fire_at);
                    let pos = if let Some(req) = target_secs {
                        active_indices
                            .iter()
                            .copied()
                            .min_by_key(|&i| s.timers[i].total_secs.abs_diff(req))
                    } else if let Some(ord) = index {
                        active_indices.get(ord).copied()
                    } else {
                        // Singular "удали таймер": remove currently displayed timer if active, else soonest
                        let cur_disp_id = fs::read_to_string(TIMER_DISPLAY_FILE)
                            .ok()
                            .and_then(|raw| Json::parse(&raw).ok())
                            .and_then(|j| j.get("timer_id").and_then(|v| v.as_i64()))
                            .unwrap_or(0) as u64;
                        active_indices
                            .iter()
                            .copied()
                            .find(|&i| s.timers[i].id == cur_disp_id)
                            .or_else(|| active_indices.first().copied())
                    };
                    let rem_item = pos.map(|i| s.timers.remove(i));
                    if rem_item.is_some() {
                        save_timers_disk(&s.timers);
                    }
                    rem_item
                };
                if let Some(t) = removed {
                    // Only clear display if the removed timer was the displayed one
                    let cur_disp_id = fs::read_to_string(TIMER_DISPLAY_FILE)
                        .ok()
                        .and_then(|raw| Json::parse(&raw).ok())
                        .and_then(|j| j.get("timer_id").and_then(|v| v.as_i64()))
                        .unwrap_or(0) as u64;
                    if cur_disp_id == t.id {
                        clear_timer_display();
                    }
                    println!("[local-ai] cancelled single timer id={} total={}s", t.id, t.total_secs);
                    show_timer_popup_async("timer_off", None);
                    let spoken = format!("Хорошо, таймер на {} отменён.", format_duration_ru(t.total_secs));
                    if !speak_via_remote_tts(&spoken, "neutral", false) {
                        if !speak_via_remote_tts("Готово.", "neutral", false) {
                            play_command_chime("cancel");
                        }
                    }
                } else {
                    if !speak_via_remote_tts("Активных таймеров сейчас нет.", "neutral", false) {
                        play_command_chime("tick");
                    }
                }
            }
        }
        LocalIntent::QueryTimers => {
            let now = now_unix_i64();
            let mut active: Vec<TimerItem> = {
                let s = store().lock().unwrap();
                s.timers.iter().filter(|t| t.fire_at > now).cloned().collect()
            };
            active.sort_by_key(|t| t.fire_at);
            if active.is_empty() {
                clear_timer_display();
                println!("[local-ai] query timers -> NO TMR");
                if !speak_via_remote_tts("Активных таймеров сейчас нет.", "neutral", false) {
                    play_command_chime("tick");
                    show_ticker_briefly("NO TMR");
                }
                return;
            }
            let first = &active[0];
            let first_rem = (first.fire_at - now).max(1) as u64;
            set_timer_display(first.id, false);
            show_timer_popup_async("timer_set", Some(first_rem.to_string()));
            if active.len() == 1 {
                let spoken = format!("До конца таймера осталось {}.", format_duration_ru(first_rem));
                println!("[local-ai] query timers (1) -> {spoken}");
                if !speak_via_remote_tts(&spoken, "neutral", false) {
                    play_command_chime("tick");
                }
            } else {
                let mut parts = vec![format!(
                    "Сейчас {} {}.",
                    active.len(),
                    ru_plural(active.len() as u64, "таймер", "таймера", "таймеров")
                )];
                for t in &active {
                    let r = (t.fire_at - now).max(1) as u64;
                    parts.push(format!(
                        "На {} — осталось {}.",
                        format_duration_ru(t.total_secs),
                        format_duration_ru(r)
                    ));
                }
                let spoken = parts.join(" ");
                println!("[local-ai] query timers ({}) -> {spoken}", active.len());
                if !speak_via_remote_tts(&spoken, "neutral", false) {
                    for t in &active {
                        let r = (t.fire_at - now).max(1) as u64;
                        let s_one = format!("До конца таймера осталось {}.", format_duration_ru(r));
                        let _ = speak_via_remote_tts(&s_one, "neutral", false);
                    }
                }
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
