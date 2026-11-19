use std::fs;
use std::os::raw::{c_int, c_void};
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;
use std::thread;
use std::time::{Duration, Instant};

use crate::json::{obj, Json};
use crate::light::SCREEN_MODE_FILE;
use crate::status::{active_error, errors_json_value};
use crate::sys::{
    accept, atomic_write_bytes, bind, close, format_local, listen, now_unix_i64, recv, reboot,
    send, setsockopt, socket, sync, tzset, uname_strings, SockAddrRc, Timeval,
    AF_BLUETOOTH, BTPROTO_RFCOMM, BT_SECURITY, RB_AUTOBOOT, SOCK_STREAM, SOL_BLUETOOTH, SOL_SOCKET,
    SO_RCVTIMEO, SO_REUSEADDR,
};

const RFCOMM_PORT: u8 = 1;
const MAX_CLIENTS: usize = 4;
const MAX_LINE_BYTES: usize = 8192;
const IDLE_TIMEOUT_SECS: u64 = 300;
const VOLUME_FILE: &str = "/var/lib/atrios/volume";
const BT_PAIRED_FILE: &str = "/var/lib/atrios/bt_paired";
const BT_PAIR_UNTIL_FILE: &str = "/var/run/atrios-bt-pair-until";
const BUTTONS_STATE_FILE: &str = "/var/run/atrios/buttons.json";
const AI_CONFIG_FILE: &str = "/var/lib/atrios/ai.json";
const AI_STATUS_FILE: &str = "/var/run/atrios/ai_status.json";
const DEVICE_FILE: &str = "/etc/atrios-device";

static CLIENT_COUNT: Mutex<usize> = Mutex::new(0);
static SCREEN_GEN: AtomicU64 = AtomicU64::new(0);
static SCREEN_LOCK: Mutex<()> = Mutex::new(());

fn read_device_meta() -> (String, String, String, String, String) {
    let mut name = "AtriStation Max".to_string();
    let mut codename = "station-max".to_string();
    let mut rev = "rev1.0".to_string();
    let mut os_name = "AtriOS Nebula".to_string();
    let mut fw = "1.0".to_string();

    if let Ok(txt) = fs::read_to_string(DEVICE_FILE) {
        for line in txt.lines().map(str::trim) {
            if let Some(v) = line.strip_prefix("DEVICE_NAME=") {
                let c = v.trim().trim_matches('"').trim_matches('\'');
                if !c.is_empty() { name = c.into(); }
            } else if let Some(v) = line.strip_prefix("DEVICE_CODENAME=") {
                let c = v.trim().trim_matches('"').trim_matches('\'');
                if !c.is_empty() { codename = c.into(); }
            } else if let Some(v) = line.strip_prefix("DEVICE_REV=") {
                let c = v.trim().trim_matches('"').trim_matches('\'');
                if !c.is_empty() { rev = c.into(); }
            }
        }
    }
    if let Ok(txt) = fs::read_to_string("/etc/os-release") {
        for line in txt.lines().map(str::trim) {
            if let Some(v) = line.strip_prefix("PRETTY_NAME=") {
                let c = v.trim().trim_matches('"').trim_matches('\'');
                if !c.is_empty() { os_name = c.into(); }
            } else if let Some(v) = line.strip_prefix("VERSION=") {
                let c = v.trim().trim_matches('"').trim_matches('\'');
                if !c.is_empty() { fw = c.into(); }
            }
        }
    }
    if let Ok(v) = fs::read_to_string("/etc/atrios-version") {
        let t = v.trim();
        if !t.is_empty() {
            fw = t.into();
        }
    }
    (name, codename, rev, os_name, fw)
}

fn get_lux() -> Option<i64> {
    let raw = crate::light::read_sensor_lux()?;
    Some((raw / 0.2) as i64)
}

fn is_mac(s: &str) -> bool {
    let parts: Vec<&str> = s.split(':').collect();
    parts.len() == 6 && parts.iter().all(|p| p.len() == 2 && p.chars().all(|c| c.is_ascii_hexdigit()))
}

struct BtPrivacy {
    discoverable: bool,
    hidden: bool,
    paired_devices: Vec<String>,
    pair_remaining_sec: i64,
}

fn get_bt_privacy() -> BtPrivacy {
    let mut paired = Vec::new();
    if let Ok(txt) = fs::read_to_string(BT_PAIRED_FILE) {
        for line in txt.lines() {
            let mac = line.trim();
            if is_mac(mac) {
                paired.push(mac.to_ascii_uppercase());
            }
        }
    }
    let mut pair_remaining = 0i64;
    if let Ok(txt) = fs::read_to_string(BT_PAIR_UNTIL_FILE) {
        if let Ok(until) = txt.trim().parse::<i64>() {
            let rem = until - now_unix_i64();
            if rem > 0 {
                pair_remaining = rem;
            }
        }
    }
    let discoverable = Command::new("hciconfig")
        .arg("hci0")
        .output()
        .map(|o| String::from_utf8_lossy(&o.stdout).contains("ISCAN"))
        .unwrap_or(false);
    let hidden = !paired.is_empty() && !discoverable && pair_remaining == 0;
    BtPrivacy { discoverable, hidden, paired_devices: paired, pair_remaining_sec: pair_remaining }
}

fn get_cpu_temp() -> f64 {
    fs::read_to_string("/sys/class/thermal/thermal_zone0/temp")
        .ok()
        .and_then(|s| s.trim().parse::<f64>().ok())
        .map(|v| (v / 100.0).round() / 10.0)
        .unwrap_or(0.0)
}

fn get_uptime_sec() -> i64 {
    fs::read_to_string("/proc/uptime")
        .ok()
        .and_then(|s| s.split_whitespace().next()?.parse::<f64>().ok())
        .map(|v| v as i64)
        .unwrap_or(0)
}

fn get_mem_info() -> (i64, i64, i64) {
    let mut total = 0i64;
    let mut free = 0i64;
    if let Ok(txt) = fs::read_to_string("/proc/meminfo") {
        for line in txt.lines() {
            if let Some(rest) = line.strip_prefix("MemTotal:") {
                if let Some(kb) = rest.split_whitespace().next().and_then(|s| s.parse::<i64>().ok()) {
                    total = kb / 1024;
                }
            } else if let Some(rest) = line.strip_prefix("MemAvailable:") {
                if let Some(kb) = rest.split_whitespace().next().and_then(|s| s.parse::<i64>().ok()) {
                    free = kb / 1024;
                }
            }
        }
        return (total, free, (total - free).max(0));
    }
    (2048, 1800, 248)
}

fn get_wifi_info() -> (String, String, bool) {
    let mut ip = String::new();
    let mut ssid = String::new();
    if let Ok(out) = Command::new("ip").args(["-4", "addr", "show", "wlan0"]).output() {
        for line in String::from_utf8_lossy(&out.stdout).lines().map(str::trim) {
            if let Some(rest) = line.strip_prefix("inet ") {
                if let Some(addr) = rest.split_whitespace().next() {
                    ip = addr.split('/').next().unwrap_or("").into();
                    break;
                }
            }
        }
    }
    if let Ok(out) = Command::new("iw").args(["dev", "wlan0", "link"]).output() {
        for line in String::from_utf8_lossy(&out.stdout).lines().map(str::trim) {
            if let Some(rest) = line.strip_prefix("SSID:") {
                ssid = rest.trim().into();
                break;
            }
        }
    }
    let connected = !ip.is_empty();
    (ip, ssid, connected)
}

fn get_current_volume() -> i32 {
    fs::read_to_string(VOLUME_FILE)
        .ok()
        .and_then(|s| s.trim().parse::<i32>().ok())
        .map(|v| v.clamp(0, 100))
        .unwrap_or(60)
}

fn set_system_volume(pct: i32) {
    let pct = pct.clamp(0, 100);
    let _ = atomic_write_bytes(Path::new(VOLUME_FILE), format!("{pct}\n").as_bytes(), 0o644, false);
    let _ = Command::new("/usr/lib/atrios/audio")
        .args(["volume", &pct.to_string()])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn();
}

fn get_ai_info() -> Json {
    let mut enabled = false;
    let mut server = "https://192.168.1.106:8090".to_string();
    let mut wake_word = "Atri".to_string();
    if let Ok(txt) = fs::read_to_string(AI_CONFIG_FILE) {
        if let Ok(j) = Json::parse(&txt) {
            if let Some(b) = j.get("enabled").and_then(|v| v.as_bool()) { enabled = b; }
            if let Some(s) = j.get_str("server") { server = s.into(); }
            if let Some(s) = j.get_str("wake_word") { wake_word = s.into(); }
        }
    }
    let mut state = if enabled { "idle" } else { "disabled" }.to_string();
    let mut last_asr = String::new();
    let mut last_answer = String::new();
    let mut error = String::new();
    if let Ok(txt) = fs::read_to_string(AI_STATUS_FILE) {
        if let Ok(j) = Json::parse(&txt) {
            if let Some(s) = j.get_str("state") { state = s.into(); }
            if let Some(s) = j.get_str("last_asr") { last_asr = s.into(); }
            if let Some(s) = j.get_str("last_answer") { last_answer = s.into(); }
            if let Some(s) = j.get_str("error") { error = s.into(); }
        }
    }
    obj(vec![
        ("enabled", Json::Bool(enabled)),
        ("server", Json::Str(server)),
        ("wake_word", Json::Str(wake_word)),
        ("state", Json::Str(state)),
        ("last_asr", Json::Str(last_asr)),
        ("last_answer", Json::Str(last_answer)),
        ("error", Json::Str(error)),
    ])
}

fn get_buttons_state() -> (bool, bool) {
    if let Ok(txt) = fs::read_to_string(BUTTONS_STATE_FILE) {
        if let Ok(j) = Json::parse(&txt) {
            let ap = j.get("action_pressed").and_then(|v| v.as_bool()).unwrap_or(false);
            let mm = j.get("mic_muted").and_then(|v| v.as_bool()).unwrap_or(false);
            return (ap, mm);
        }
    }
    (false, false)
}

fn get_screen_mode() -> String {
    fs::read_to_string(SCREEN_MODE_FILE)
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "clock".into())
}

fn set_screen_mode(mode: &str) {
    let _ = fs::create_dir_all("/var/run/atrios");
    let _ = fs::write(SCREEN_MODE_FILE, mode);
}

fn build_status_payload() -> Json {
    let vol = get_current_volume();
    let (dev_name, codename, rev, os_name, fw_ver) = read_device_meta();
    let bt_priv = get_bt_privacy();
    unsafe { tzset() };
    let tz_name = fs::canonicalize("/etc/localtime")
        .ok()
        .and_then(|p| {
            let s = p.to_string_lossy().into_owned();
            s.split("zoneinfo/").nth(1).map(|x| x.to_string())
        })
        .unwrap_or_else(|| "UTC".into());
    let (kver, arch) = uname_strings();
    let (mem_tot, mem_free, mem_used) = get_mem_info();
    let (ip, ssid, wifi_conn) = get_wifi_info();
    let (ap, mm) = get_buttons_state();
    let bt_spk = crate::light::is_bt_active() || !crate::tools::dbus_bluealsa_pcms().is_empty();

    obj(vec![
        ("errors", errors_json_value()),
        ("type", Json::Str("status".into())),
        (
            "device",
            obj(vec![
                ("model", Json::Str(dev_name.clone())),
                ("codename", Json::Str(codename)),
                ("rev", Json::Str(rev)),
                ("soc", Json::Str("Amlogic S905X3 (SM1) quad Cortex-A55".into())),
                ("arch", Json::Str(arch)),
                ("os", Json::Str(os_name)),
                ("fw_version", Json::Str(fw_ver)),
                ("kernel", Json::Str(kver)),
                ("uptime_seconds", Json::Num(get_uptime_sec() as f64)),
                ("local_time", Json::Str(format_local("%Y-%m-%d %H:%M:%S %Z", None))),
                ("timezone", Json::Str(tz_name)),
                ("cpu_temperature", Json::Num(get_cpu_temp())),
                ("lux", get_lux().map(|v| Json::Num(v as f64)).unwrap_or(Json::Null)),
                (
                    "memory",
                    obj(vec![
                        ("total_mb", Json::Num(mem_tot as f64)),
                        ("free_mb", Json::Num(mem_free as f64)),
                        ("used_mb", Json::Num(mem_used as f64)),
                    ]),
                ),
            ]),
        ),
        (
            "audio",
            obj(vec![
                ("volume", Json::Num(vol as f64)),
                ("mute", Json::Bool(vol == 0)),
                ("card", Json::Str("AML-AUGESOUND".into())),
                ("speakers", Json::Str("Silergy SY6045S (Woofer 30W + Tweeters 2x10W, 20V PVDD)".into())),
            ]),
        ),
        (
            "wifi",
            obj(vec![
                ("ip", Json::Str(ip)),
                ("ssid", Json::Str(ssid)),
                ("connected", Json::Bool(wifi_conn)),
            ]),
        ),
        (
            "buttons",
            obj(vec![("action_pressed", Json::Bool(ap)), ("mic_muted", Json::Bool(mm))]),
        ),
        ("screen_mode", Json::Str(get_screen_mode())),
        ("ai", get_ai_info()),
        (
            "bluetooth",
            obj(vec![
                ("adapter", Json::Str("hci0".into())),
                ("name", Json::Str(dev_name)),
                ("transport", Json::Str("RFCOMM SPP (Channel 1)".into())),
                ("is_speaker_mode", Json::Bool(bt_spk)),
                ("discoverable", Json::Bool(bt_priv.discoverable)),
                ("hidden", Json::Bool(bt_priv.hidden)),
                ("paired_devices", Json::Arr(bt_priv.paired_devices.into_iter().map(Json::Str).collect())),
                ("pair_remaining_sec", Json::Num(bt_priv.pair_remaining_sec as f64)),
            ]),
        ),
    ])
}

fn send_json_line(fd: c_int, j: &Json) -> bool {
    let pkt = format!("{}\n", j.to_string_compact());
    let data = pkt.as_bytes();
    let mut off = 0;
    while off < data.len() {
        let n = unsafe { send(fd, data[off..].as_ptr() as *const c_void, data.len() - off, 0) };
        if n <= 0 {
            return false;
        }
        off += n as usize;
    }
    true
}

fn process_command(cmd_obj: &Json) -> Result<Json, String> {
    let Some(map) = cmd_obj.as_obj() else {
        return Err("Command must be a JSON object".into());
    };
    let cmd = map
        .get("cmd")
        .or_else(|| map.get("type"))
        .and_then(|v| v.as_str())
        .unwrap_or("");
    match cmd {
        "get_status" | "status" => Ok(build_status_payload()),
        "set_volume" | "volume" => {
            if let Some(v) = map.get("value").or_else(|| map.get("volume")).and_then(|x| x.as_i64()) {
                set_system_volume(v as i32);
                Ok(obj(vec![
                    ("type", Json::Str("volume_response".into())),
                    ("success", Json::Bool(true)),
                    ("volume", Json::Num(get_current_volume() as f64)),
                ]))
            } else {
                Err("Missing volume".into())
            }
        }
        "set_mute" | "mute" => {
            let is_mute = map
                .get("value")
                .or_else(|| map.get("mute"))
                .and_then(|v| v.as_bool())
                .unwrap_or(true);
            set_system_volume(if is_mute { 0 } else { 50 });
            Ok(obj(vec![
                ("type", Json::Str("mute_response".into())),
                ("success", Json::Bool(true)),
                ("mute", Json::Bool(get_current_volume() == 0)),
            ]))
        }
        "audio_test" => {
            let _ = Command::new("/usr/lib/atrios/audio")
                .arg("test")
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn();
            Ok(obj(vec![
                ("type", Json::Str("action_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str("Audio chime triggered".into())),
            ]))
        }
        "sync_time" => {
            let _ = Command::new("/usr/bin/atrios")
                .args(["time", "sync"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            Ok(build_status_payload())
        }
        "reboot" => {
            thread::spawn(|| {
                thread::sleep(Duration::from_secs(1));
                unsafe {
                    sync();
                    reboot(RB_AUTOBOOT);
                }
            });
            Ok(obj(vec![
                ("type", Json::Str("action_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str("Rebooting in 1s".into())),
            ]))
        }
        "set_screen" => {
            let mode = map.get("mode").and_then(|v| v.as_str()).unwrap_or("clock");
            if !matches!(mode, "clock" | "ticker" | "eyes" | "kisser" | "off") {
                return Err("Unsupported screen mode".into());
            }
            let generation = {
                let _g = SCREEN_LOCK.lock().unwrap();
                let gen = SCREEN_GEN.fetch_add(1, Ordering::SeqCst) + 1;
                let _ = Command::new("pkill")
                    .args(["-f", "/usr/lib/atrios/light screen"])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .status();
                set_screen_mode(mode);
                if mode == "ticker" {
                    let text: String = map
                        .get("text")
                        .and_then(|v| v.as_str())
                        .unwrap_or("AtriOS")
                        .chars()
                        .take(512)
                        .collect();
                    if let Ok(mut child) = Command::new("/usr/bin/atrios")
                        .args(["screen", "ticker", &text])
                        .stdout(Stdio::null())
                        .stderr(Stdio::null())
                        .spawn()
                    {
                        thread::spawn(move || {
                            let _ = child.wait();
                            let _g = SCREEN_LOCK.lock().unwrap();
                            if SCREEN_GEN.load(Ordering::SeqCst) == gen && get_screen_mode() == "ticker" {
                                set_screen_mode("clock");
                            }
                        });
                    }
                } else if mode == "eyes" {
                    let st = map.get("state").and_then(|v| v.as_str()).unwrap_or("happy");
                    let _ = Command::new("/usr/bin/atrios")
                        .args(["screen", "eyes", st])
                        .stdout(Stdio::null())
                        .stderr(Stdio::null())
                        .spawn();
                } else if mode == "kisser" || mode == "off" {
                    let _ = Command::new("/usr/bin/atrios")
                        .args(["screen", mode])
                        .stdout(Stdio::null())
                        .stderr(Stdio::null())
                        .spawn();
                }
                gen
            };
            let _ = generation;
            Ok(build_status_payload())
        }
        "set_ring" => {
            if active_error().is_some()
                || get_buttons_state().1
                || Path::new("/var/run/atrios/ring_override").exists()
            {
                return Ok(obj(vec![
                    ("type", Json::Str("action_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str("Ring is displaying an error, microphone mute or volume".into())),
                ]));
            }
            let mode = map.get("mode").and_then(|v| v.as_str()).unwrap_or("off");
            let _ = Command::new("pkill")
                .args(["-f", "/usr/lib/atrios/light ring"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            if mode == "volume" {
                let _ = Command::new("/usr/bin/atrios")
                    .args(["ring", "volume", &get_current_volume().to_string()])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .spawn();
            } else if matches!(mode, "rainbow" | "amber" | "amber_green" | "red" | "blue" | "green" | "off") {
                let _ = fs::create_dir_all("/var/run/atrios");
                let _ = fs::write("/var/run/atrios/ring_mode", mode);
                let mut cmd = Command::new("/usr/bin/atrios");
                cmd.args(["ring", mode]);
                if mode == "amber" || mode == "amber_green" {
                    cmd.arg("0");
                }
                let _ = cmd.stdout(Stdio::null()).stderr(Stdio::null()).spawn();
            }
            Ok(obj(vec![
                ("type", Json::Str("action_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str(format!("Ring: {mode}"))),
            ]))
        }
        "set_ai" => {
            if let Some(srv) = map.get("server").and_then(|v| v.as_str()).map(str::trim).filter(|s| !s.is_empty()) {
                let _ = Command::new("/usr/bin/atrios").args(["ai", "server", srv]).status();
            }
            if let Some(ww) = map.get("wake_word").and_then(|v| v.as_str()) {
                let w = if ww.trim().is_empty() { "off" } else { ww.trim() };
                let _ = Command::new("/usr/bin/atrios").args(["ai", "wake", w]).status();
            }
            if let Some(en) = map.get("enabled").and_then(|v| v.as_bool()) {
                let _ = Command::new("/usr/bin/atrios")
                    .args(["ai", if en { "enable" } else { "disable" }])
                    .status();
                let _ = Command::new("/etc/init.d/S80atriai")
                    .arg(if en { "restart" } else { "stop" })
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .spawn();
            }
            Ok(build_status_payload())
        }
        "bt_pair" => {
            let secs = map.get("seconds").and_then(|v| v.as_i64()).unwrap_or(60).clamp(10, 300);
            let _ = Command::new("/usr/bin/atrios").args(["bt", "pair", &secs.to_string()]).status();
            Ok(build_status_payload())
        }
        "bt_hide" => {
            let _ = Command::new("/usr/bin/atrios").args(["bt", "hide"]).status();
            Ok(build_status_payload())
        }
        "bt_unpair" => {
            let mac = map.get("mac").and_then(|v| v.as_str()).unwrap_or("all");
            let _ = Command::new("/usr/bin/atrios").args(["bt", "unpair", mac]).status();
            Ok(build_status_payload())
        }
        "wifi_connect" => {
            let ssid = map.get("ssid").and_then(|v| v.as_str()).unwrap_or("");
            let psk = map.get("psk").and_then(|v| v.as_str()).unwrap_or("");
            if !ssid.is_empty() {
                let _ = Command::new("/usr/lib/atrios/wireless")
                    .args(["wifi", "connect", ssid, psk])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .spawn();
                return Ok(obj(vec![
                    ("type", Json::Str("action_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str(format!("Connecting to {ssid}..."))),
                ]));
            }
            Err("Missing SSID".into())
        }
        _ => Err(format!("Unknown command: {cmd}")),
    }
}

fn handle_client(fd: c_int, mac: String) {
    println!("[bt-control] Client connected via Bluetooth RFCOMM: {mac}");
    if !send_json_line(fd, &build_status_payload()) {
        unsafe { close(fd) };
        return;
    }
    let tv = Timeval { tv_sec: 0, tv_usec: 500_000 };
    unsafe {
        setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &tv as *const Timeval as *const c_void,
            std::mem::size_of::<Timeval>() as u32,
        );
    }

    let mut buf: Vec<u8> = Vec::new();
    let mut tmp = [0u8; 1024];
    let mut last_input = Instant::now();
    let mut last_hb = Instant::now();
    let mut last_vol = get_current_volume();
    let mut last_btns = get_buttons_state();

    loop {
        if last_input.elapsed().as_secs() > IDLE_TIMEOUT_SECS {
            break;
        }
        let n = unsafe { recv(fd, tmp.as_mut_ptr() as *mut c_void, tmp.len(), 0) };
        if n == 0 {
            break;
        }
        if n > 0 {
            last_input = Instant::now();
            buf.extend_from_slice(&tmp[..n as usize]);
            if buf.len() > MAX_LINE_BYTES {
                break;
            }
            while let Some(pos) = buf.iter().position(|&b| b == b'\n') {
                let raw: Vec<u8> = buf.drain(..=pos).collect();
                let line = String::from_utf8_lossy(&raw[..raw.len() - 1]).trim().to_string();
                if line.is_empty() {
                    continue;
                }
                match Json::parse(&line).and_then(|c| process_command(&c)) {
                    Ok(resp) => {
                        if !send_json_line(fd, &resp) {
                            unsafe { close(fd) };
                            return;
                        }
                    }
                    Err(e) => {
                        let _ = send_json_line(fd, &obj(vec![("type", Json::Str("error".into())), ("message", Json::Str(e))]));
                    }
                }
            }
        } else {
            let err = std::io::Error::last_os_error();
            if err.kind() != std::io::ErrorKind::WouldBlock && err.kind() != std::io::ErrorKind::TimedOut {
                break;
            }
        }

        let cur_vol = get_current_volume();
        if cur_vol != last_vol {
            last_vol = cur_vol;
            if !send_json_line(
                fd,
                &obj(vec![
                    ("type", Json::Str("volume_change".into())),
                    ("volume", Json::Num(cur_vol as f64)),
                    ("mute", Json::Bool(cur_vol == 0)),
                ]),
            ) {
                break;
            }
        }
        let cur_btns = get_buttons_state();
        if cur_btns != last_btns {
            last_btns = cur_btns;
            if !send_json_line(
                fd,
                &obj(vec![
                    ("type", Json::Str("buttons_change".into())),
                    ("action_pressed", Json::Bool(cur_btns.0)),
                    ("mic_muted", Json::Bool(cur_btns.1)),
                ]),
            ) {
                break;
            }
        }
        if last_hb.elapsed() >= Duration::from_secs(4) {
            last_hb = Instant::now();
            if !send_json_line(fd, &build_status_payload()) {
                break;
            }
        }
    }
    unsafe { close(fd) };
    println!("[bt-control] Client disconnected: {mac}");
}

pub fn run(_args: &[String]) -> i32 {
    let server_fd = unsafe { socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM) };
    if server_fd < 0 {
        eprintln!("[bt-control] RFCOMM socket failed: {}", std::io::Error::last_os_error());
        return 1;
    }
    let one: c_int = 1;
    let sec = [2u8, 0u8];
    unsafe {
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one as *const c_int as *const c_void, 4);
        setsockopt(server_fd, SOL_BLUETOOTH, BT_SECURITY, sec.as_ptr() as *const c_void, 2);
    }
    let addr = SockAddrRc {
        rc_family: AF_BLUETOOTH as u16,
        rc_bdaddr: [0u8; 6],
        rc_channel: RFCOMM_PORT,
        _pad: 0,
    };
    if unsafe {
        bind(
            server_fd,
            &addr as *const SockAddrRc as *const c_void,
            std::mem::size_of::<SockAddrRc>() as u32,
        )
    } != 0
    {
        eprintln!("[bt-control] RFCOMM bind failed: {}", std::io::Error::last_os_error());
        unsafe { close(server_fd) };
        return 1;
    }
    unsafe { listen(server_fd, MAX_CLIENTS as c_int) };
    println!("[bt-control] Listening for Android App connections on Bluetooth RFCOMM channel {RFCOMM_PORT}...");

    loop {
        let mut peer = SockAddrRc { rc_family: 0, rc_bdaddr: [0; 6], rc_channel: 0, _pad: 0 };
        let mut plen = std::mem::size_of::<SockAddrRc>() as u32;
        let client_fd = unsafe { accept(server_fd, &mut peer as *mut SockAddrRc as *mut c_void, &mut plen) };
        if client_fd < 0 {
            thread::sleep(Duration::from_secs(1));
            continue;
        }
        let mac = format!(
            "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
            peer.rc_bdaddr[5], peer.rc_bdaddr[4], peer.rc_bdaddr[3],
            peer.rc_bdaddr[2], peer.rc_bdaddr[1], peer.rc_bdaddr[0]
        );
        let priv_info = get_bt_privacy();
        let auth = priv_info.paired_devices.contains(&mac) || priv_info.pair_remaining_sec > 0;
        let slot_ok = {
            let mut c = CLIENT_COUNT.lock().unwrap();
            if auth && *c < MAX_CLIENTS {
                *c += 1;
                true
            } else {
                false
            }
        };
        if !slot_ok {
            unsafe { close(client_fd) };
            continue;
        }
        thread::spawn(move || {
            handle_client(client_fd, mac);
            let mut c = CLIENT_COUNT.lock().unwrap();
            *c = c.saturating_sub(1);
        });
    }
}
