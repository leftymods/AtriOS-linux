use std::collections::BTreeMap;
use std::ffi::CString;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::raw::c_void;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::{AsRawFd, RawFd};
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime};

use crate::json::{obj, Json};
use crate::light::{fb_ring, RING_LEDS, RING_OVERRIDE_FILE};
use crate::status::active_error;
use crate::sys::{
    atomic_write_bytes, close, format_local, ioctl, kill, makedev, mknod, mmap, munmap,
    now_unix_secs, poll, reboot, recv_or_read, socket, sync, tzset, InputEvent, PollFd, AF_INET,
    I2C_SLAVE_FORCE, MAP_SHARED, POLLIN, PROT_READ, PROT_WRITE, RB_AUTOBOOT, RB_POWER_OFF,
    SIGUSR1, SIGUSR2, SIOCGIFFLAGS, SIOCSIFFLAGS, SOCK_DGRAM, WIFI_POWER_IOCTL,
};

const CARD: &str = "AMLAUGESOUND";
const VOLUME_STEP: i32 = 5;
const STATE_FILE: &str = "/var/lib/atrios/volume";
const DEFAULT_VOLUME: i32 = 60;
const BUTTONS_STATE_FILE: &str = "/var/run/atrios/buttons.json";

const KEY_VOICECOMMAND: u16 = 582;
const KEY_POWER: u16 = 116;
const KEY_MICMUTE: u16 = 248;
const KEY_PAGEUP: u16 = 104;
const KEY_PAGEDOWN: u16 = 109;
const EV_KEY: u16 = 1;
const MIC_MUTE_ACTIVE_LOW: bool = true;

#[derive(Clone)]
struct SharedState {
    mic_muted: bool,
    action_pressed: bool,
    ring_active: bool,
    current_volume: i32,
    display_volume: f64,
    last_volume_time: f64,
    volume_dirty: bool,
    wake_seq: u64,
}

pub fn volume_to_raw(pct: i32, zero_db_raw: i32) -> i32 {
    let pct = pct.clamp(0, 100);
    if pct <= 0 {
        return 0;
    }
    let db = -40.0 * (1.0 - ((pct as f64) / 100.0).powf(0.65));
    let raw = ((zero_db_raw as f64) + 2.0 * db).round() as i32;
    raw.clamp(1, zero_db_raw)
}

pub fn woofer_volume_to_raw(pct: i32, zero_db_raw: i32) -> i32 {
    let pct = pct.clamp(0, 100);
    if pct <= 0 {
        return 0;
    }
    let raw = volume_to_raw(pct, zero_db_raw);
    if pct < 30 {
        let boost = (16.0 * (1.0 - (pct as f64) / 30.0)).round() as i32;
        return (raw + boost).min(zero_db_raw);
    }
    if pct <= 40 {
        return raw;
    }
    // Dynamic bass limiting above 40%: keep tweeters scaling full range while softly capping
    // woofer excursion to +2.0 dB (+4 raw steps) from 40% to 100% so bass stays tight & clean.
    let base_40 = volume_to_raw(40, zero_db_raw);
    let extra = ((((pct - 40) as f64) / 60.0).powf(0.75) * 4.0).round() as i32;
    (base_40 + extra).clamp(1, zero_db_raw)
}

fn read_state_volume() -> Option<i32> {
    fs::read_to_string(STATE_FILE)
        .ok()
        .and_then(|s| s.trim().parse::<i32>().ok())
        .map(|v| v.clamp(0, 100))
}

fn save_state_volume(pct: i32) {
    let _ = atomic_write_bytes(Path::new(STATE_FILE), format!("{pct}\n").as_bytes(), 0o644, false);
}

fn write_buttons_state(action_pressed: bool, mic_muted: bool) {
    let body = obj(vec![
        ("action_pressed", Json::Bool(action_pressed)),
        ("mic_muted", Json::Bool(mic_muted)),
    ])
    .to_string_compact();
    let _ = atomic_write_bytes(Path::new(BUTTONS_STATE_FILE), body.as_bytes(), 0o644, false);
}

fn amixer_run(card: &str, args: &[&str]) {
    let _ = Command::new("amixer")
        .arg("-c")
        .arg(card)
        .args(args)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
}

fn read_alsa_controls() -> Option<(i32, i32)> {
    let mut vals = Vec::with_capacity(2);
    for ctl in ["Woofer Master Playback Volume", "Tweeters Master Playback Volume"] {
        let out = Command::new("amixer")
            .args(["-c", CARD, "cget", &format!("name={ctl}")])
            .stderr(Stdio::null())
            .output()
            .ok()?;
        let txt = String::from_utf8_lossy(&out.stdout);
        let idx = txt.find(": values=")?;
        let rest = &txt[idx + 9..];
        let num_str: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        vals.push(num_str.parse::<i32>().ok()?);
    }
    Some((vals[0], vals[1]))
}

fn set_volume_i2c(last_raw: &Mutex<Option<i32>>, pct: i32, force_switches: bool) {
    let pct = pct.clamp(0, 100);
    let raw = volume_to_raw(pct, 255);
    let raw_wfr = woofer_volume_to_raw(pct, 255);
    let raw_hp = volume_to_raw(pct, 191);
    let mute_i2c: u8 = if pct == 0 { 0x08 } else { 0x00 };
    let mute_es: u8 = if pct == 0 { 0x00 } else { 0x11 };

    let Ok(mut f) = OpenOptions::new().read(true).write(true).open("/dev/i2c-2") else {
        return;
    };
    let fd = f.as_raw_fd();
    let mut guard = last_raw.lock().unwrap();
    let mut vals = Vec::new();
    if let Some(prev) = *guard {
        if (raw - prev).abs() > 2 {
            let step = if raw > prev { 2 } else { -2 };
            let mut cur = prev + step;
            while (step > 0 && cur < raw) || (step < 0 && cur > raw) {
                vals.push(cur);
                cur += step;
            }
        }
    }
    vals.push(raw);
    let boost = raw_wfr - raw;
    let multi = vals.len() > 1;
    for v in vals {
        unsafe { ioctl(fd, I2C_SLAVE_FORCE, 0x2ausize) };
        let _ = f.write_all(&[0x07, v as u8]);
        let vw = if v == 0 { 0 } else { (v + boost).min(255) };
        unsafe { ioctl(fd, I2C_SLAVE_FORCE, 0x2busize) };
        let _ = f.write_all(&[0x07, vw as u8]);
        if multi {
            thread::sleep(Duration::from_millis(1));
        }
    }
    if force_switches {
        for addr in [0x2ausize, 0x2b] {
            unsafe { ioctl(fd, I2C_SLAVE_FORCE, addr) };
            let _ = f.write_all(&[0x06, mute_i2c]);
        }
    }
    *guard = Some(raw);
    unsafe { ioctl(fd, I2C_SLAVE_FORCE, 0x08usize) };
    let _ = f.write_all(&[0x14, raw_hp as u8]);
    if force_switches {
        let _ = f.write_all(&[0x13, mute_es]);
    }
}

fn set_volume_full(
    alsa_lock: &Mutex<()>,
    last_raw: &Mutex<Option<i32>>,
    pct: i32,
    force_switches: bool,
) {
    let pct = pct.clamp(0, 100);
    set_volume_i2c(last_raw, pct, force_switches);
    let _g = alsa_lock.lock().unwrap();
    let raw = volume_to_raw(pct, 255);
    let raw_wfr = woofer_volume_to_raw(pct, 255);
    let sw = if pct == 0 { "0" } else { "1" };
    let master_sw = if pct == 0 { "0" } else { "3" };
    for card in [CARD, "0"] {
        let ok = Command::new("amixer")
            .args([
                "-c",
                card,
                "cset",
                "name=Woofer Master Playback Volume",
                &raw_wfr.to_string(),
            ])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        amixer_run(card, &["cset", "name=Tweeters Master Playback Volume", &raw.to_string()]);
        if force_switches {
            for ctl in [
                "Woofer Ch1 Playback Volume",
                "Woofer Ch2 Playback Volume",
                "Tweeters Ch1 Playback Volume",
                "Tweeters Ch2 Playback Volume",
            ] {
                amixer_run(card, &["cset", &format!("name={ctl}"), "159"]);
            }
            for ctl in ["Woofer Master Playback Switch", "Tweeters Master Playback Switch"] {
                amixer_run(card, &["cset", &format!("name={ctl}"), master_sw]);
            }
            for ctl in [
                "Woofer Ch1 Playback Switch",
                "Woofer Ch2 Playback Switch",
                "Tweeters Ch1 Playback Switch",
                "Tweeters Ch2 Playback Switch",
            ] {
                amixer_run(card, &["cset", &format!("name={ctl}"), sw]);
            }
        }
        if ok {
            break;
        }
    }
    let _ = Command::new("wpctl")
        .env("XDG_RUNTIME_DIR", "/var/run/pipewire")
        .args(["set-volume", "@DEFAULT_AUDIO_SINK@", "1.00"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
    if force_switches {
        let _ = Command::new("wpctl")
            .env("XDG_RUNTIME_DIR", "/var/run/pipewire")
            .args(["set-mute", "@DEFAULT_AUDIO_SINK@", if pct == 0 { "1" } else { "0" }])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
    }
}

fn set_ring_frame(fd_cache: &mut Option<File>, frame: &[u8]) {
    if fd_cache.is_none() {
        let path = fb_ring();
        *fd_cache = OpenOptions::new().write(true).open(path).ok();
    }
    if let Some(ref mut f) = fd_cache {
        if f.seek(SeekFrom::Start(0)).is_ok() && f.write_all(frame).is_ok() {
            return;
        }
    }
    *fd_cache = None;
}

fn calc_led_color(ratio: f64) -> (u8, u8) {
    if ratio < 0.5 {
        ((255.0 * ratio * 2.0) as u8, 255)
    } else {
        (255, (255.0 * (2.0 - ratio * 2.0)) as u8)
    }
}

fn show_volume_ring(fd_cache: &mut Option<File>, vol_pct: f64, brightness: f64) {
    if vol_pct <= 0.0 || brightness <= 0.001 {
        set_ring_frame(fd_cache, &[0u8; RING_LEDS * 3]);
        return;
    }
    let br = brightness.clamp(0.0, 1.0);
    let exact = (RING_LEDS as f64) * vol_pct.clamp(0.0, 100.0) / 100.0;
    let full = (exact as usize).min(RING_LEDS);
    let frac = exact - (full as f64);
    let mut frame = vec![0u8; RING_LEDS * 3];
    for i in 0..full {
        let ratio = (i as f64) / ((RING_LEDS - 1).max(1) as f64);
        let (r, g) = calc_led_color(ratio);
        frame[i * 3 + 0] = 0;
        frame[i * 3 + 1] = ((g as f64) * br) as u8;
        frame[i * 3 + 2] = ((r as f64) * br) as u8;
    }
    if full < RING_LEDS && frac > 0.02 {
        let eased = frac.powf(1.4) * br;
        let ratio = (full as f64) / ((RING_LEDS - 1).max(1) as f64);
        let (r, g) = calc_led_color(ratio);
        frame[full * 3 + 0] = 0;
        frame[full * 3 + 1] = ((g as f64) * eased) as u8;
        frame[full * 3 + 2] = ((r as f64) * eased) as u8;
    }
    set_ring_frame(fd_cache, &frame);
}

fn enter_volume_ring_override() {
    let _ = fs::create_dir_all("/var/run/atrios");
    let _ = fs::write(RING_OVERRIDE_FILE, "volume");
    let _ = Command::new("pkill")
        .args(["-f", "/usr/lib/atrios/light ring"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status();
}

fn exit_volume_ring_override() {
    let _ = fs::remove_file(RING_OVERRIDE_FILE);
    let ai_active = fs::read_to_string("/var/run/atrios/ai_status.json")
        .ok()
        .and_then(|s| Json::parse(&s).ok())
        .and_then(|j| j.get_str("state").map(|s| matches!(s, "thinking" | "speaking")))
        .unwrap_or(false);
    if let Ok(pid_str) = fs::read_to_string("/var/run/atrios/atriai.pid") {
        if let Ok(pid) = pid_str.trim().parse::<i32>() {
            unsafe { kill(pid, SIGUSR2) };
        }
    }
    if !ai_active {
        if let Ok(mode) = fs::read_to_string("/var/run/atrios/ring_mode") {
            let m = mode.trim();
            if matches!(m, "amber" | "amber_green") {
                let _ = Command::new("/usr/bin/atrios")
                    .args(["ring", m, "0"])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .spawn();
            } else if matches!(m, "rainbow" | "red" | "blue" | "green") {
                let _ = Command::new("/usr/bin/atrios")
                    .args(["ring", m])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .spawn();
            }
        }
    }
}

fn set_software_mic_mute(muted: bool, action_pressed: bool) {
    write_buttons_state(action_pressed, muted);
    let alsa_cmd = if muted { "mute" } else { "unmute" };
    let alsa_vol = if muted { "0%" } else { "100%" };
    let sw_val = if muted { "0" } else { "1" };
    let adc_mute = if muted { "1" } else { "0" };
    for card in [CARD, "0"] {
        amixer_run(card, &["sset", "Capture", alsa_cmd]);
        amixer_run(card, &["sset", "Capture", alsa_vol]);
        for ctl in [
            "Capture Switch",
            "ADC Capture Switch",
            "Mic Capture Switch",
            "TDMIN_A Switch",
            "TDMIN_B Switch",
        ] {
            amixer_run(card, &["cset", &format!("name={ctl}"), sw_val]);
        }
        for i in 1..=4 {
            amixer_run(card, &["cset", &format!("name=Loopback ADC{i}_MUTE"), adc_mute]);
        }
    }
}

fn ensure_input_nodes() {
    let _ = fs::create_dir_all("/dev/input");
    let Ok(entries) = fs::read_dir("/sys/class/input") else { return };
    for e in entries.flatten() {
        let name = e.file_name();
        let Some(ns) = name.to_str() else { continue };
        if !ns.starts_with("event") {
            continue;
        }
        let dev_node = format!("/dev/input/{ns}");
        if Path::new(&dev_node).exists() {
            continue;
        }
        if let Ok(dev_txt) = fs::read_to_string(e.path().join("dev")) {
            let parts: Vec<&str> = dev_txt.trim().split(':').collect();
            if parts.len() == 2 {
                if let (Ok(maj), Ok(min)) = (parts[0].parse::<u32>(), parts[1].parse::<u32>()) {
                    if let Ok(c_path) = CString::new(dev_node) {
                        unsafe {
                            mknod(c_path.as_ptr(), 0o020000 | 0o660, makedev(maj, min));
                        }
                    }
                }
            }
        }
    }
}

fn scan_input_devices(open_fds: &mut BTreeMap<RawFd, String>) {
    ensure_input_nodes();
    let Ok(entries) = fs::read_dir("/dev/input") else { return };
    let mut paths: Vec<String> = entries
        .flatten()
        .filter_map(|e| e.file_name().to_str().map(|s| format!("/dev/input/{s}")))
        .filter(|p| p.contains("/event"))
        .collect();
    paths.sort();
    for path in paths {
        if open_fds.values().any(|v| v == &path) {
            continue;
        }
        if let Ok(f) = OpenOptions::new().read(true).custom_flags(0o4000).open(&path) {
            let fd = f.as_raw_fd();
            std::mem::forget(f);
            open_fds.insert(fd, path);
        }
    }
}

fn init_ring_triggers() {
    let Ok(entries) = fs::read_dir("/sys/class/leds") else { return };
    for e in entries.flatten() {
        if let Some(n) = e.file_name().to_str() {
            if n.contains("ring") {
                let _ = fs::write(e.path().join("trigger"), "led_ring_fb\n");
            }
        }
    }
}

fn run_daemon() -> i32 {
    let mut open_fds: BTreeMap<RawFd, String> = BTreeMap::new();
    init_ring_triggers();
    scan_input_devices(&mut open_fds);

    let initial_vol = match read_state_volume() {
        Some(v) => v,
        None => {
            save_state_volume(DEFAULT_VOLUME);
            DEFAULT_VOLUME
        }
    };

    let shared = Arc::new((
        Mutex::new(SharedState {
            mic_muted: false,
            action_pressed: false,
            ring_active: false,
            current_volume: initial_vol,
            display_volume: initial_vol as f64,
            last_volume_time: 0.0,
            volume_dirty: false,
            wake_seq: 0,
        }),
        Condvar::new(),
    ));
    let alsa_lock = Arc::new(Mutex::new(()));
    let last_raw = Arc::new(Mutex::new(None::<i32>));

    {
        let mut fd_cache = None;
        set_ring_frame(&mut fd_cache, &[0u8; RING_LEDS * 3]);
    }
    set_volume_full(&alsa_lock, &last_raw, initial_vol, true);
    write_buttons_state(false, false);

    // Spawn dedicated 40 FPS LED ring renderer thread
    let render_shared = Arc::clone(&shared);
    thread::spawn(move || {
        let mut fd_cache: Option<File> = None;
        let mut last_anim = Instant::now();
        let mut override_active = false;
        let mut error_active = false;
        let mut last_drawn: Option<(i32, i32)> = None;
        let mut seen_seq = 0u64;

        loop {
            let st = render_shared.0.lock().unwrap().clone();
            let err = active_error();
            if err.is_some() || st.mic_muted {
                if !error_active {
                    enter_volume_ring_override();
                    error_active = true;
                }
                let level: u8 = if st.mic_muted {
                    90
                } else if (now_unix_secs() * 2.0) as i64 % 2 == 0 {
                    100
                } else {
                    12
                };
                let c = if st.mic_muted { [0u8, 0, level] } else { [0u8, level / 2, level] };
                let mut frame = Vec::with_capacity(RING_LEDS * 3);
                for _ in 0..RING_LEDS {
                    frame.extend_from_slice(&c);
                }
                set_ring_frame(&mut fd_cache, &frame);
                let mut guard = render_shared.0.lock().unwrap();
                let res = render_shared
                    .1
                    .wait_timeout_while(guard, Duration::from_millis(250), |s| s.wake_seq == seen_seq)
                    .unwrap();
                guard = res.0;
                seen_seq = guard.wake_seq;
                continue;
            }
            if error_active {
                set_ring_frame(&mut fd_cache, &[0u8; RING_LEDS * 3]);
                exit_volume_ring_override();
                error_active = false;
                override_active = false;
            }
            let active = st.last_volume_time > 0.0 || (st.display_volume - (st.current_volume as f64)).abs() > 0.05;
            if !active {
                if override_active {
                    override_active = false;
                    exit_volume_ring_override();
                }
                last_drawn = None;
                let guard = render_shared.0.lock().unwrap();
                let res = render_shared
                    .1
                    .wait_timeout_while(guard, Duration::from_millis(500), |s| s.wake_seq == seen_seq)
                    .unwrap();
                seen_seq = res.0.wake_seq;
                last_anim = Instant::now();
                continue;
            }
            if !override_active {
                override_active = true;
                enter_volume_ring_override();
            }
            let now_inst = Instant::now();
            let dt = now_inst.duration_since(last_anim).as_secs_f64().clamp(0.001, 0.08);
            last_anim = now_inst;

            let mut disp = st.display_volume;
            let diff = (st.current_volume as f64) - disp;
            if diff.abs() > 1.5 {
                disp += diff * (dt * 28.0).min(1.0);
            } else {
                disp = st.current_volume as f64;
            }

            let elapsed = if st.last_volume_time > 0.0 {
                (now_unix_secs() - st.last_volume_time).max(0.0)
            } else {
                0.0
            };
            let fade_factor = if elapsed > 0.8 {
                let t = ((elapsed - 0.8) / 0.35).min(1.0);
                if t < 1.0 { 1.0 - (3.0 * t * t - 2.0 * t * t * t) } else { 0.0 }
            } else {
                1.0
            };

            let t0 = Instant::now();
            if fade_factor <= 0.001 {
                set_ring_frame(&mut fd_cache, &[0u8; RING_LEDS * 3]);
                {
                    let mut g = render_shared.0.lock().unwrap();
                    g.last_volume_time = 0.0;
                    g.ring_active = false;
                    g.display_volume = disp;
                }
                last_drawn = None;
                if override_active {
                    override_active = false;
                    exit_volume_ring_override();
                }
            } else {
                {
                    let mut g = render_shared.0.lock().unwrap();
                    g.display_volume = disp;
                }
                let key = ((disp * 100.0) as i32, (fade_factor * 1000.0) as i32);
                if Some(key) != last_drawn {
                    show_volume_ring(&mut fd_cache, disp, fade_factor);
                    last_drawn = Some(key);
                }
            }
            let spent = t0.elapsed();
            if spent < Duration::from_millis(25) {
                let rem = Duration::from_millis(25) - spent;
                let guard = render_shared.0.lock().unwrap();
                let res = render_shared
                    .1
                    .wait_timeout_while(guard, rem, |s| s.wake_seq == seen_seq)
                    .unwrap();
                seen_seq = res.0.wake_seq;
            }
        }
    });

    let mut top_press_time = 0.0f64;
    let mut last_enforce = Instant::now();
    let mut last_scan = Instant::now();
    let mut last_flush = Instant::now();
    let mut last_state_mtime: Option<SystemTime> = fs::metadata(STATE_FILE).and_then(|m| m.modified()).ok();

    let ev_sz = std::mem::size_of::<InputEvent>();
    let mut ev_buf = vec![0u8; ev_sz * 64];

    loop {
        let mut pfds: Vec<PollFd> = open_fds.keys().map(|&fd| PollFd { fd, events: POLLIN, revents: 0 }).collect();
        if pfds.is_empty() {
            thread::sleep(Duration::from_millis(50));
        } else {
            unsafe { poll(pfds.as_mut_ptr(), pfds.len() as _, 50) };
        }
        let now_unix = now_unix_secs();

        for pfd in pfds {
            if (pfd.revents & POLLIN) == 0 {
                continue;
            }
            let n = unsafe {
                std::mem::transmute::<isize, isize>(crate::sys::recv_or_read(pfd.fd, &mut ev_buf))
            };
            if n <= 0 {
                let err = std::io::Error::last_os_error();
                if err.kind() != std::io::ErrorKind::WouldBlock {
                    unsafe { close(pfd.fd) };
                    open_fds.remove(&pfd.fd);
                }
                continue;
            }
            let mut net_step = 0i32;
            let count = (n as usize) / ev_sz;
            for i in 0..count {
                let ev = unsafe {
                    std::ptr::read_unaligned(ev_buf[i * ev_sz..].as_ptr() as *const InputEvent)
                };
                if ev.ev_type != EV_KEY {
                    continue;
                }
                if ev.code == KEY_PAGEUP && ev.value == 1 {
                    net_step -= VOLUME_STEP;
                } else if ev.code == KEY_PAGEDOWN && ev.value == 1 {
                    net_step += VOLUME_STEP;
                } else if ev.code == KEY_MICMUTE && (ev.value == 0 || ev.value == 1) {
                    let pressed = if MIC_MUTE_ACTIVE_LOW { ev.value == 0 } else { ev.value == 1 };
                    let ap = {
                        let mut g = shared.0.lock().unwrap();
                        g.mic_muted = pressed;
                        g.wake_seq += 1;
                        g.action_pressed
                    };
                    shared.1.notify_all();
                    set_software_mic_mute(pressed, ap);
                } else if ev.code == KEY_VOICECOMMAND || ev.code == KEY_POWER {
                    if ev.value == 1 {
                        top_press_time = now_unix;
                        let mm = {
                            let mut g = shared.0.lock().unwrap();
                            g.action_pressed = true;
                            g.mic_muted
                        };
                        write_buttons_state(true, mm);
                    } else if ev.value == 0 {
                        let mm = {
                            let mut g = shared.0.lock().unwrap();
                            g.action_pressed = false;
                            g.mic_muted
                        };
                        write_buttons_state(false, mm);
                        if (now_unix - top_press_time) < 2.5 {
                            let ai_enabled = fs::read_to_string("/var/lib/atrios/ai.json")
                                .ok()
                                .and_then(|s| Json::parse(&s).ok())
                                .and_then(|j| j.get("enabled")?.as_bool())
                                .unwrap_or(false);
                            if ai_enabled {
                                let _ = fs::write("/var/run/atrios/ai_trigger", format!("{now_unix}"));
                                if let Ok(p) = fs::read_to_string("/var/run/atrios/atriai.pid") {
                                    if let Ok(pid) = p.trim().parse::<i32>() {
                                        unsafe { kill(pid, SIGUSR1) };
                                    }
                                }
                            } else {
                                let _ = Command::new("playerctl")
                                    .arg("play-pause")
                                    .stdout(Stdio::null())
                                    .stderr(Stdio::null())
                                    .spawn();
                            }
                        }
                    }
                }
            }
            if net_step != 0 {
                let (prev, nxt) = {
                    let mut g = shared.0.lock().unwrap();
                    let prev = g.current_volume;
                    g.current_volume = (g.current_volume + net_step).clamp(0, 100);
                    g.ring_active = true;
                    g.last_volume_time = now_unix_secs();
                    g.volume_dirty = true;
                    g.wake_seq += 1;
                    (prev, g.current_volume)
                };
                set_volume_i2c(&last_raw, nxt, prev == 0 || nxt == 0);
                shared.1.notify_all();
            }
        }

        // Fast state flush
        let dirty_now = shared.0.lock().unwrap().volume_dirty;
        if dirty_now && last_flush.elapsed() > Duration::from_millis(80) {
            let vol = {
                let mut g = shared.0.lock().unwrap();
                g.volume_dirty = false;
                g.current_volume
            };
            save_state_volume(vol);
            let al = Arc::clone(&alsa_lock);
            let lr = Arc::clone(&last_raw);
            thread::spawn(move || set_volume_full(&al, &lr, vol, false));
            last_flush = Instant::now();
            last_state_mtime = fs::metadata(STATE_FILE).and_then(|m| m.modified()).ok();
        }

        // Fast sync on external STATE_FILE update
        let (is_dirty, is_ring, cur_vol) = {
            let g = shared.0.lock().unwrap();
            (g.volume_dirty, g.ring_active, g.current_volume)
        };
        if !is_dirty {
            if let Ok(mtime) = fs::metadata(STATE_FILE).and_then(|m| m.modified()) {
                if Some(mtime) != last_state_mtime {
                    last_state_mtime = Some(mtime);
                    if let Some(saved) = read_state_volume() {
                        if saved != cur_vol {
                            let force_sw = cur_vol == 0 || saved == 0;
                            {
                                let mut g = shared.0.lock().unwrap();
                                g.current_volume = saved;
                                g.ring_active = true;
                                g.last_volume_time = now_unix_secs();
                                g.wake_seq += 1;
                            }
                            set_volume_i2c(&last_raw, saved, force_sw);
                            shared.1.notify_all();
                            let al = Arc::clone(&alsa_lock);
                            let lr = Arc::clone(&last_raw);
                            thread::spawn(move || set_volume_full(&al, &lr, saved, force_sw));
                            last_enforce = Instant::now();
                        }
                    }
                }
            }
        }

        // Watchdog every 2s
        if !is_ring && !is_dirty && last_enforce.elapsed() > Duration::from_secs(2) {
            last_enforce = Instant::now();
            if let Some(saved) = read_state_volume() {
                if saved != cur_vol {
                    let force_sw = cur_vol == 0 || saved == 0;
                    {
                        let mut g = shared.0.lock().unwrap();
                        g.current_volume = saved;
                        g.ring_active = true;
                        g.last_volume_time = now_unix_secs();
                        g.wake_seq += 1;
                    }
                    set_volume_i2c(&last_raw, saved, force_sw);
                    shared.1.notify_all();
                    let al = Arc::clone(&alsa_lock);
                    let lr = Arc::clone(&last_raw);
                    thread::spawn(move || set_volume_full(&al, &lr, saved, force_sw));
                } else if let Some((act_wfr, act_twt)) = read_alsa_controls() {
                    let exp_wfr = woofer_volume_to_raw(cur_vol, 255);
                    let exp_twt = volume_to_raw(cur_vol, 255);
                    if (act_wfr - exp_wfr).abs() > 1 || (act_twt - exp_twt).abs() > 1 {
                        let al = Arc::clone(&alsa_lock);
                        let lr = Arc::clone(&last_raw);
                        thread::spawn(move || set_volume_full(&al, &lr, cur_vol, false));
                    }
                }
            }
        }

        if last_scan.elapsed() > Duration::from_secs(3) {
            scan_input_devices(&mut open_fds);
            last_scan = Instant::now();
        }
    }
}

// --- Built-in hardware & init subcommands replacing inline python3 -c calls ---

fn cmd_init_amps() -> i32 {
    // 1. SM1 TDM_B pad mux
    if let Ok(f) = OpenOptions::new().read(true).write(true).open("/dev/mem") {
        unsafe {
            let ptr = mmap(
                std::ptr::null_mut(),
                0x1000,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                f.as_raw_fd(),
                0xFF66_0000,
            );
            if ptr != usize::MAX as *mut c_void && !ptr.is_null() {
                let p24 = (ptr as *mut u8).add(0x24) as *mut u32;
                let p20 = (ptr as *mut u8).add(0x20) as *mut u32;
                std::ptr::write_volatile(p24, 0x0111_0111u32.to_le());
                std::ptr::write_volatile(p20, 0x0000_0011u32.to_le());
                munmap(ptr, 0x1000);
            }
        }
    }
    // 2. Load SY6045S firmware settings via /dev/i2c-2
    let load_sy = |path: &str, addr: usize| {
        let Ok(txt) = fs::read_to_string(path) else { return };
        let Ok(mut i2c) = OpenOptions::new().read(true).write(true).open("/dev/i2c-2") else { return };
        unsafe { ioctl(i2c.as_raw_fd(), I2C_SLAVE_FORCE, addr) };
        for line in txt.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.is_empty() {
                continue;
            }
            let bytes_slice = if parts[0].eq_ignore_ascii_case("w") && parts.len() >= 4 {
                &parts[2..]
            } else if parts.len() >= 2 && !matches!(parts[0].to_ascii_lowercase().as_str(), "w" | "d" | "r") {
                &parts[..]
            } else {
                continue;
            };
            let mut raw = Vec::with_capacity(bytes_slice.len());
            let mut ok = true;
            for tok in bytes_slice {
                let clean = tok.trim_start_matches("0x").trim_start_matches("0X");
                if let Ok(b) = u8::from_str_radix(clean, 16) {
                    raw.push(b);
                } else {
                    ok = false;
                    break;
                }
            }
            if ok && !raw.is_empty() {
                let _ = i2c.write_all(&raw);
            }
        }
        let _ = i2c.write_all(&[0x22, 0x00]);
        let _ = i2c.write_all(&[0x06, 0x00]);
        let _ = i2c.write_all(&[0x02, 0x00]);
    };
    load_sy("/lib/firmware/sy6045s-tweeters-settings.txt", 0x2A);
    load_sy("/lib/firmware/sy6045s-woofer-settings.txt", 0x2B);
    0
}

fn cmd_resize_mbr(args: &[String]) -> i32 {
    if args.len() < 5 {
        return 1;
    }
    let disk_dev = &args[0];
    let Ok(part_num) = args[1].parse::<usize>() else { return 1 };
    let Ok(cur_start) = args[2].parse::<u32>() else { return 1 };
    let Ok(cur_size) = args[3].parse::<u32>() else { return 1 };
    let Ok(target_size) = args[4].parse::<u32>() else { return 1 };
    if !(1..=4).contains(&part_num) {
        return 1;
    }
    let Ok(mut f) = OpenOptions::new().read(true).write(true).custom_flags(0o4010000).open(disk_dev) else {
        return 1;
    };
    let mut mbr = [0u8; 512];
    if f.read_exact(&mut mbr).is_err() || mbr[510] != 0x55 || mbr[511] != 0xAA {
        return 1;
    }
    let off = 0x1BE + (part_num - 1) * 16;
    let ptype = mbr[off + 4];
    let mbr_start = u32::from_le_bytes(mbr[off + 8..off + 12].try_into().unwrap());
    let mbr_size = u32::from_le_bytes(mbr[off + 12..off + 16].try_into().unwrap());
    if ptype == 0 || mbr_start != cur_start || mbr_size != cur_size {
        return 1;
    }
    let target_end = cur_start.saturating_add(target_size);
    for idx in 0..4 {
        if idx == part_num - 1 {
            continue;
        }
        let o = 0x1BE + idx * 16;
        if mbr[o + 4] != 0 {
            let s = u32::from_le_bytes(mbr[o + 8..o + 12].try_into().unwrap());
            if s >= cur_start && s < target_end {
                return 1;
            }
        }
    }
    mbr[off + 12..off + 16].copy_from_slice(&target_size.to_le_bytes());
    mbr[off + 5] = 0xFE;
    mbr[off + 6] = 0xFF;
    mbr[off + 7] = 0xFF;
    if f.seek(SeekFrom::Start(0)).is_err() || f.write_all(&mbr).is_err() || f.sync_all().is_err() {
        return 1;
    }
    0
}

fn cmd_ifup(iface: &str) -> i32 {
    let fd = unsafe { socket(AF_INET, SOCK_DGRAM, 0) };
    if fd < 0 {
        return 1;
    }
    let mut ifr = [0u8; 40];
    let b = iface.as_bytes();
    let copy_len = b.len().min(15);
    ifr[..copy_len].copy_from_slice(&b[..copy_len]);
    let rc = unsafe {
        if ioctl(fd, SIOCGIFFLAGS, ifr.as_mut_ptr()) != 0 {
            close(fd);
            return 1;
        }
        let flags = u16::from_ne_bytes([ifr[16], ifr[17]]) | 1;
        ifr[16..18].copy_from_slice(&flags.to_ne_bytes());
        let r = ioctl(fd, SIOCSIFFLAGS, ifr.as_mut_ptr());
        close(fd);
        r
    };
    if rc == 0 { 0 } else { 1 }
}

fn cmd_wifi_power_ioctl() -> i32 {
    let Ok(f) = OpenOptions::new().write(true).open("/dev/wifi_power") else {
        return 1;
    };
    let rc = unsafe { ioctl(f.as_raw_fd(), WIFI_POWER_IOCTL, 0usize) };
    if rc == 0 { 0 } else { 1 }
}

fn cmd_woofer_raw(args: &[String]) -> i32 {
    let pct = args.first().and_then(|s| s.parse::<i32>().ok()).unwrap_or(0);
    let zero_db = args.get(1).and_then(|s| s.parse::<i32>().ok()).unwrap_or(255);
    println!("{}", woofer_volume_to_raw(pct, zero_db));
    0
}

fn cmd_volume_raw(args: &[String]) -> i32 {
    let pct = args
        .first()
        .and_then(|s| s.trim().parse::<f64>().ok())
        .unwrap_or(0.0);
    let z = args
        .get(1)
        .and_then(|s| s.trim().parse::<f64>().ok())
        .unwrap_or(255.0);
    if pct <= 0.0 {
        println!("0");
    } else {
        let db = -40.0 * (1.0 - (pct / 100.0).powf(0.65));
        let max_r = (z as i32).max(1);
        let r = ((z + 2.0 * db).round() as i32).clamp(1, max_r);
        println!("{r}");
    }
    0
}

fn cmd_input_monitor() -> i32 {
    unsafe { tzset() };
    let mut open_fds: BTreeMap<RawFd, String> = BTreeMap::new();
    scan_input_devices(&mut open_fds);
    if open_fds.is_empty() {
        return 1;
    }

    let mut names: BTreeMap<RawFd, String> = BTreeMap::new();
    for (&fd, path) in &open_fds {
        let base = Path::new(path)
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("event");
        let name_path = format!("/sys/class/input/{base}/device/name");
        let name = fs::read_to_string(&name_path)
            .ok()
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| base.to_string());
        names.insert(fd, name);
    }

    let ev_sz = std::mem::size_of::<InputEvent>();
    let mut ev_buf = vec![0u8; ev_sz * 64];
    let mut mic_muted = false;
    let mut press_t = 0.0f64;

    loop {
        if open_fds.is_empty() {
            return 1;
        }
        let mut pfds: Vec<PollFd> = open_fds
            .keys()
            .map(|&fd| PollFd { fd, events: POLLIN, revents: 0 })
            .collect();
        unsafe { poll(pfds.as_mut_ptr(), pfds.len() as _, 100) };

        for pfd in pfds {
            if (pfd.revents & POLLIN) == 0 {
                continue;
            }
            let n = recv_or_read(pfd.fd, &mut ev_buf);
            if n <= 0 {
                let err = std::io::Error::last_os_error();
                if err.kind() != std::io::ErrorKind::WouldBlock {
                    unsafe { close(pfd.fd) };
                    open_fds.remove(&pfd.fd);
                }
                continue;
            }
            let dev_name = names.get(&pfd.fd).map(String::as_str).unwrap_or("event");
            let count = (n as usize) / ev_sz;
            for i in 0..count {
                let ev = unsafe {
                    std::ptr::read_unaligned(ev_buf[i * ev_sz..].as_ptr() as *const InputEvent)
                };
                if ev.ev_type != EV_KEY {
                    continue;
                }
                let ts = format_local("%H:%M:%S", None);
                if ev.code == KEY_MICMUTE {
                    let active_edge = if MIC_MUTE_ACTIVE_LOW { 0 } else { 1 };
                    if ev.value == active_edge {
                        mic_muted = !mic_muted;
                        let st = if mic_muted { "MUTED" } else { "UNMUTED" };
                        println!("[{ts}] >>> TOP BUTTON #2 (MIC MUTE) PRESSED -> {st} <<<");
                    } else {
                        println!("[{ts}] [RELEASED] KEY_MICMUTE (Top Button #2)");
                    }
                } else if ev.code == KEY_VOICECOMMAND || ev.code == KEY_POWER {
                    if ev.value == 1 {
                        press_t = now_unix_secs();
                        println!("[{ts}] >>> TOP BUTTON #1 (ASSISTANT) PRESSED <<<");
                    } else if ev.value == 0 {
                        let dur = if press_t > 0.0 { now_unix_secs() - press_t } else { 0.0 };
                        println!("[{ts}] Top Button #1 released ({dur:.2}s)");
                    }
                } else if (ev.code == KEY_PAGEUP || ev.code == KEY_PAGEDOWN) && ev.value == 1 {
                    let d = if ev.code == KEY_PAGEUP {
                        "Clockwise (+Vol)"
                    } else {
                        "Counter-Clockwise (-Vol)"
                    };
                    let extra = match read_state_volume() {
                        Some(pct) => format!(" [{pct}%]"),
                        None => String::new(),
                    };
                    println!("[{ts}] Rotary Knob Turned: {d}{extra}");
                } else {
                    let kname = match ev.code {
                        582 => "KEY_VOICECOMMAND (Top Button #1: Alice / Assistant)".to_string(),
                        116 => "KEY_POWER (Top Button #1 Alt: Assistant / Power)".to_string(),
                        248 => "KEY_MICMUTE (Top Button #2: Physical Mic Mute)".to_string(),
                        104 => "KEY_PAGEUP (Rotary Knob Clockwise -> Volume Up)".to_string(),
                        109 => "KEY_PAGEDOWN (Rotary Knob Counter-Clockwise -> Volume Down)".to_string(),
                        256 => "BTN_MISC / BTN_0 (Hardware VAD Trigger)".to_string(),
                        115 => "KEY_VOLUMEUP (Remote Vol+)".to_string(),
                        114 => "KEY_VOLUMEDOWN (Remote Vol-)".to_string(),
                        c => format!("KEY_{c}"),
                    };
                    let st = match ev.value {
                        1 => "PRESSED",
                        0 => "RELEASED",
                        _ => "REPEAT",
                    };
                    println!("[{ts}] [{st}] {kname} ({dev_name})");
                }
                let _ = std::io::stdout().flush();
            }
        }
    }
}

pub fn run(args: &[String]) -> i32 {
    match args.first().map(String::as_str) {
        Some("init-amps") => cmd_init_amps(),
        Some("resize-mbr") => cmd_resize_mbr(&args[1..]),
        Some("ifup") => cmd_ifup(args.get(1).map(String::as_str).unwrap_or("wlan0")),
        Some("wifi-power-ioctl") => cmd_wifi_power_ioctl(),
        Some("volume-raw") => cmd_volume_raw(&args[1..]),
        Some("woofer-raw") => cmd_woofer_raw(&args[1..]),
        Some("input-monitor") => cmd_input_monitor(),
        Some("sys-reboot") => unsafe {
            sync();
            reboot(RB_AUTOBOOT);
            0
        },
        Some("sys-poweroff") => unsafe {
            sync();
            reboot(RB_POWER_OFF);
            0
        },
        _ => run_daemon(),
    }
}
