use std::fs;
use std::thread;
use std::time::{Duration, Instant};

use crate::json::Json;
use crate::light::{
    is_bt_active, render_clock_frame, render_error_frame, render_timer_frame,
    screen_send_frame_raw, SCREEN_MODE_FILE,
};
use crate::status::active_error;
use crate::sys::{format_local, now_unix_i64, tzset};

const TIMERS_FILE: &str = "/var/run/atrios/timers.json";
const TIMER_DISPLAY_FILE: &str = "/var/run/atrios/timer_display.json";

fn get_screen_mode() -> String {
    fs::read_to_string(SCREEN_MODE_FILE)
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "clock".into())
}

/// Returns (remaining_secs, total_secs) ONLY when timer_display.json requests displaying
/// an active timer (either pinned == true, or now < until). Auto-hides back to clock after timeout.
fn active_displayed_timer() -> Option<(u64, u64)> {
    let disp_raw = fs::read_to_string(TIMER_DISPLAY_FILE).ok()?;
    let dj = Json::parse(&disp_raw).ok()?;
    let timer_id = dj.get("timer_id").and_then(|v| v.as_i64()).unwrap_or(0) as u64;
    let until = dj.get("until").and_then(|v| v.as_i64()).unwrap_or(0);
    let pinned = dj.get("pinned").and_then(|v| v.as_bool()).unwrap_or(false);
    let now = now_unix_i64();
    if !pinned && now >= until {
        return None;
    }

    let raw = fs::read_to_string(TIMERS_FILE).ok()?;
    let j = Json::parse(&raw).ok()?;
    let arr = j.as_arr()?;
    // Prefer exact timer_id; if not found (or 0), fall back to soonest active timer
    let mut exact: Option<(u64, u64)> = None;
    let mut soonest: Option<(u64, u64)> = None;
    for item in arr {
        let id = item.get("id").and_then(|v| v.as_i64()).unwrap_or(0) as u64;
        let fire_at = item.get("fire_at").and_then(|v| v.as_i64()).unwrap_or(0);
        let total = item.get("total_secs").and_then(|v| v.as_i64()).unwrap_or(1).max(1) as u64;
        if fire_at > now {
            let rem = (fire_at - now) as u64;
            if timer_id != 0 && id == timer_id {
                exact = Some((rem, total));
                break;
            }
            if soonest.map(|(b_rem, _)| rem < b_rem).unwrap_or(true) {
                soonest = Some((rem, total));
            }
        }
    }
    exact.or(soonest)
}

pub fn run(_args: &[String]) -> i32 {
    let mut last_rendered = String::new();
    let mut last_bt: Option<bool> = None;
    let mut last_tz: Option<std::path::PathBuf> = None;
    let mut prev_timer: Option<(u64, u64)> = None;
    let t0 = Instant::now();

    loop {
        if let Some(err) = active_error() {
            let var = match err.code {
                1 => "wifi",
                2 | 3 => "no_internet",
                4 => "audio_err",
                5 => "update",
                7 => "kernel_panic",
                _ => err.kind.as_str(),
            };
            let buf = render_error_frame(var, t0.elapsed().as_secs_f64(), 200);
            screen_send_frame_raw(&buf);
            last_rendered.clear();
            thread::sleep(Duration::from_millis(1000 / 15));
            continue;
        }
        if get_screen_mode() != "clock" {
            last_rendered.clear();
            thread::sleep(Duration::from_millis(250));
            continue;
        }
        let cur_tz = fs::canonicalize("/etc/localtime").ok();
        if cur_tz != last_tz {
            unsafe { tzset() };
            last_tz = cur_tz;
        }
        let bt = is_bt_active();
        if let Some((rem, total)) = active_displayed_timer() {
            prev_timer = Some((rem, total));
            let colon_on = (t0.elapsed().as_millis() / 500) % 2 == 0;
            let key = format!("T:{rem}:{total}:{colon_on}:{bt}");
            if key != last_rendered {
                let buf = render_timer_frame(rem, total, colon_on, bt, 200, 2, None);
                screen_send_frame_raw(&buf);
                last_rendered = key;
                last_bt = Some(bt);
            }
            thread::sleep(Duration::from_millis(250));
            continue;
        }
        // Smooth slide-down + fade-out hide transition when timer auto-hides after 5s
        if let Some((rem, total)) = prev_timer.take() {
            for step in 1..=8 {
                let p = step as f64 / 8.0;
                let y_off = 2 + (12.0 * p * p).round() as isize;
                let br = ((1.0 - p) * 200.0).round().clamp(0.0, 200.0) as u8;
                let buf = render_timer_frame(rem, total, true, bt, br, y_off, None);
                screen_send_frame_raw(&buf);
                thread::sleep(Duration::from_millis(40));
            }
            last_rendered.clear();
        }

        let now = format_local("%H:%M", None);
        if now != last_rendered || Some(bt) != last_bt {
            let buf = render_clock_frame(&now, bt, 200);
            screen_send_frame_raw(&buf);
            last_rendered = now;
            last_bt = Some(bt);
        }
        thread::sleep(Duration::from_millis(500));
    }
}
