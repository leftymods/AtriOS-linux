use std::collections::BTreeMap;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;
use std::process::Command;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::thread;
use std::time::{Duration, Instant};

use crate::json::{obj, Json};
use crate::sys::{atomic_write_bytes, kill, now_unix_i64, signal, SIGINT, SIGTERM};

pub const HEALTH_FILE: &str = "/var/run/atrios/health.json";
const WATCHDOG_DEV: &str = "/dev/watchdog";
const HEALTHY_RESET_SECS: u64 = 60;

static STOP_REQ: AtomicBool = AtomicBool::new(false);
static CHILD_PID: AtomicI32 = AtomicI32::new(0);

extern "C" fn handle_stop(_sig: i32) {
    STOP_REQ.store(true, Ordering::SeqCst);
    let pid = CHILD_PID.load(Ordering::SeqCst);
    if pid > 0 {
        unsafe {
            kill(pid, SIGTERM);
        }
    }
}

/// Exponential backoff: 1s, 2s, 4s, 8s, 16s, capped at `max_secs`.
pub fn next_backoff_secs(consecutive_failures: u32, max_secs: u64) -> u64 {
    let cap = max_secs.max(1);
    let shift = consecutive_failures.saturating_sub(1).min(10);
    (1u64 << shift).min(cap)
}

pub fn format_health_entry(
    state: &str,
    pid: u32,
    restarts: u64,
    last_exit: Option<i32>,
    started_ts: i64,
    updated_ts: i64,
) -> Json {
    obj(vec![
        ("state", Json::Str(state.to_string())),
        ("pid", Json::Num(pid as f64)),
        ("restarts", Json::Num(restarts as f64)),
        (
            "last_exit",
            match last_exit {
                Some(code) => Json::Num(code as f64),
                None => Json::Null,
            },
        ),
        ("started_ts", Json::Num(started_ts as f64)),
        ("updated_ts", Json::Num(updated_ts as f64)),
    ])
}

pub fn update_health_file(path: &Path, service: &str, entry: Json) {
    let mut map: BTreeMap<String, Json> = fs::read_to_string(path)
        .ok()
        .and_then(|s| Json::parse(&s).ok())
        .and_then(|j| match j {
            Json::Obj(m) => Some(m),
            _ => None,
        })
        .unwrap_or_default();
    map.insert(service.to_string(), entry);
    let payload = format!("{}
", Json::Obj(map).to_string_pretty());
    let _ = atomic_write_bytes(path, payload.as_bytes(), 0o644, false);
}

fn kick_watchdog() {
    if let Ok(mut f) = OpenOptions::new().write(true).open(WATCHDOG_DEV) {
        let _ = f.write_all(b"1");
    }
}

pub fn run(args: &[String]) -> i32 {
    let mut name: Option<String> = None;
    let mut max_backoff: u64 = 30;
    let mut use_watchdog = false;
    let mut health_path = HEALTH_FILE.to_string();
    let mut cmd_start = 0usize;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--name" if i + 1 < args.len() => {
                name = Some(args[i + 1].clone());
                i += 2;
            }
            "--max-backoff" if i + 1 < args.len() => {
                if let Ok(v) = args[i + 1].parse::<u64>() {
                    max_backoff = v.clamp(1, 300);
                }
                i += 2;
            }
            "--health-file" if i + 1 < args.len() => {
                health_path = args[i + 1].clone();
                i += 2;
            }
            "--watchdog" => {
                use_watchdog = true;
                i += 1;
            }
            "--" => {
                cmd_start = i + 1;
                break;
            }
            _ => {
                cmd_start = i;
                break;
            }
        }
    }

    if cmd_start >= args.len() {
        eprintln!(
            "Usage: supervise [--name NAME] [--max-backoff SECS] [--watchdog] -- <cmd> [args...]"
        );
        return 2;
    }

    let prog = &args[cmd_start];
    let prog_args = &args[cmd_start + 1..];
    let service = name.unwrap_or_else(|| {
        Path::new(prog)
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("service")
            .to_string()
    });

    STOP_REQ.store(false, Ordering::SeqCst);
    unsafe {
        signal(SIGTERM, handle_stop as *const () as usize);
        signal(SIGINT, handle_stop as *const () as usize);
    }

    let health_p = Path::new(&health_path);
    let mut restarts: u64 = 0;
    let mut consecutive_failures: u32 = 0;
    let mut last_exit: Option<i32> = None;

    while !STOP_REQ.load(Ordering::SeqCst) {
        let started_ts = now_unix_i64();
        let started_at = Instant::now();
        match Command::new(prog).args(prog_args).spawn() {
            Ok(mut child) => {
                let pid = child.id();
                CHILD_PID.store(pid as i32, Ordering::SeqCst);
                update_health_file(
                    health_p,
                    &service,
                    format_health_entry("running", pid, restarts, last_exit, started_ts, started_ts),
                );

                loop {
                    if use_watchdog {
                        kick_watchdog();
                    }
                    match child.try_wait() {
                        Ok(Some(status)) => {
                            last_exit = status.code().or(Some(-1));
                            break;
                        }
                        Ok(None) => {
                            if STOP_REQ.load(Ordering::SeqCst) {
                                let _ = child.kill();
                                let _ = child.wait();
                                break;
                            }
                            thread::sleep(Duration::from_millis(500));
                        }
                        Err(_) => {
                            last_exit = Some(-1);
                            break;
                        }
                    }
                }
                CHILD_PID.store(0, Ordering::SeqCst);
            }
            Err(e) => {
                eprintln!("[supervise:{service}] spawn failed for {prog}: {e}");
                last_exit = Some(127);
            }
        }

        if STOP_REQ.load(Ordering::SeqCst) {
            update_health_file(
                health_p,
                &service,
                format_health_entry("stopped", 0, restarts, last_exit, started_ts, now_unix_i64()),
            );
            break;
        }

        restarts = restarts.saturating_add(1);
        if started_at.elapsed() >= Duration::from_secs(HEALTHY_RESET_SECS) {
            consecutive_failures = 1;
        } else {
            consecutive_failures = consecutive_failures.saturating_add(1);
        }
        let delay = next_backoff_secs(consecutive_failures, max_backoff);
        update_health_file(
            health_p,
            &service,
            format_health_entry("restarting", 0, restarts, last_exit, started_ts, now_unix_i64()),
        );
        eprintln!(
            "[supervise:{service}] exited ({:?}); restarting in {}s (total restarts={})",
            last_exit, delay, restarts
        );
        for _ in 0..(delay * 2) {
            if STOP_REQ.load(Ordering::SeqCst) {
                break;
            }
            if use_watchdog {
                kick_watchdog();
            }
            thread::sleep(Duration::from_millis(500));
        }
    }

    0
}
