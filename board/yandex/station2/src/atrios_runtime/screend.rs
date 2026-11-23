use std::fs;
use std::thread;
use std::time::{Duration, Instant};

use crate::json::Json;
use crate::light::{
    is_bt_active, render_clock_frame, render_clock_with_timer_bar, render_error_frame,
    render_timer_frame, screen_send_frame_raw, SCREEN_MODE_FILE,
};
use crate::status::active_error;
use crate::sys::{format_local, now_unix_i64, tzset};

const TIMERS_FILE: &str = "/var/run/atrios/timers.json";

fn get_screen_mode() -> String {
    fs::read_to_string(SCREEN_MODE_FILE)
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "clock".into())
}

/// Returns (remaining_secs, total_secs) for the soonest active timer, if any.
fn soonest_active_timer() -> Option<(u64, u64)> {
    let raw = fs::read_to_string(TIMERS_FILE).ok()?;
    let j = Json::parse(&raw).ok()?;
    let arr = j.as_arr()?;
    let now = now_unix_i64();
    let mut best: Option<(u64, u64)> = None;
    for item in arr {
        let fire_at = item.get("fire_at").and_then(|v| v.as_i64()).unwrap_or(0);
        let total = item.get("total_secs").and_then(|v| v.as_i64()).unwrap_or(1).max(1) as u64;
        if fire_at > now {
            let rem = (fire_at - now) as u64;
            if best.map(|(b_rem, _)| rem < b_rem).unwrap_or(true) {
                best = Some((rem, total));
            }
        }
    }
    best
}

pub fn run(_args: &[String]) -> i32 {
    let mut last_rendered = String::new();
    let mut last_bt: Option<bool> = None;
    let mut last_tz: Option<std::path::PathBuf> = None;
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
        if let Some((rem, total)) = soonest_active_timer() {
            // If <= 60s left, lock on live MM:SS countdown.
            // If > 60s left, show live countdown 8s / clock+timer-bar 4s cycle.
            let elapsed_s = t0.elapsed().as_secs();
            let show_timer = rem <= 60 || (elapsed_s % 12) < 8;
            let colon_on = (t0.elapsed().as_millis() / 500) % 2 == 0;
            if show_timer {
                let key = format!("T:{rem}:{total}:{colon_on}:{bt}");
                if key != last_rendered {
                    let buf = render_timer_frame(rem, total, colon_on, bt, 200, 2, None);
                    screen_send_frame_raw(&buf);
                    last_rendered = key;
                    last_bt = Some(bt);
                }
            } else {
                let now = format_local("%H:%M", None);
                let key = format!("CT:{now}:{rem}:{total}:{bt}");
                if key != last_rendered {
                    let buf = render_clock_with_timer_bar(&now, bt, 200, rem, total);
                    screen_send_frame_raw(&buf);
                    last_rendered = key;
                    last_bt = Some(bt);
                }
            }
            thread::sleep(Duration::from_millis(250));
            continue;
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
