use std::fs;
use std::thread;
use std::time::{Duration, Instant};

use crate::light::{
    is_bt_active, render_clock_frame, render_error_frame, screen_send_frame_raw, SCREEN_MODE_FILE,
};
use crate::status::active_error;
use crate::sys::{format_local, tzset};

fn get_screen_mode() -> String {
    fs::read_to_string(SCREEN_MODE_FILE)
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "clock".into())
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
            thread::sleep(Duration::from_millis(500));
            continue;
        }
        let cur_tz = fs::canonicalize("/etc/localtime").ok();
        if cur_tz != last_tz {
            unsafe { tzset() };
            last_tz = cur_tz;
        }
        let now = format_local("%H:%M", None);
        let bt = is_bt_active();
        if now != last_rendered || Some(bt) != last_bt {
            let buf = render_clock_frame(&now, bt, 200);
            screen_send_frame_raw(&buf);
            last_rendered = now;
            last_bt = Some(bt);
        }
        thread::sleep(Duration::from_secs(1));
    }
}
