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
const TIMERS_FILE: &str = "/var/run/atrios/timers.json";
const ALARMS_FILE: &str = "/var/lib/atrios/alarms.json";
const BT_ACTIVE_FILE: &str = "/var/run/atrios/bt_active";
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

fn apply_volume_hw_fast(pct: i32) {
    use std::io::Write;
    let pct = pct.clamp(0, 100);
    let raw = if pct == 0 { 0 } else { crate::rotaryd::volume_to_raw(pct, 255) };
    let raw_wfr = if pct == 0 { 0 } else { crate::rotaryd::woofer_volume_to_raw(pct, 255) };
    let raw_hp = if pct == 0 { 0 } else { crate::rotaryd::volume_to_raw(pct, 191) };
    let sw = if pct == 0 { 0 } else { 1 };
    let msw = if pct == 0 { 0 } else { 3 };
    let spk_mute = if pct == 0 { 1 } else { 0 };
    let script = format!(
        "cset name='Woofer Master Playback Volume' {raw_wfr}\n\
         cset name='Tweeters Master Playback Volume' {raw}\n\
         cset name='Headphone DAC Playback Volume' {raw_hp}\n\
         cset name='Woofer Master Playback Switch' {msw}\n\
         cset name='Tweeters Master Playback Switch' {msw}\n\
         cset name='Woofer Ch1 Playback Switch' {sw}\n\
         cset name='Woofer Ch2 Playback Switch' {sw}\n\
         cset name='Tweeters Ch1 Playback Switch' {sw}\n\
         cset name='Tweeters Ch2 Playback Switch' {sw}\n\
         cset name='Headphone Headphone Switch' {sw}\n\
         cset name='SPK mute' {spk_mute}\n"
    );
    if let Ok(mut child) = Command::new("amixer")
        .args(["-q", "-c", "AMLAUGESOUND", "-s"])
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    {
        if let Some(mut stdin) = child.stdin.take() {
            let _ = stdin.write_all(script.as_bytes());
        }
        let _ = child.wait();
    }
}

fn set_system_volume(pct: i32) {
    use std::sync::atomic::AtomicI32;
    use std::sync::OnceLock;
    static PENDING_VOL: AtomicI32 = AtomicI32::new(-1);
    static WORKER_INIT: OnceLock<()> = OnceLock::new();

    let pct = pct.clamp(0, 100);
    let _ = atomic_write_bytes(Path::new(VOLUME_FILE), format!("{pct}\n").as_bytes(), 0o644, false);
    PENDING_VOL.store(pct, Ordering::SeqCst);

    WORKER_INIT.get_or_init(|| {
        thread::spawn(|| {
            let mut last_applied = -1i32;
            loop {
                let target = PENDING_VOL.swap(-1, Ordering::SeqCst);
                if target >= 0 && target != last_applied {
                    apply_volume_hw_fast(target);
                    last_applied = target;
                } else {
                    thread::sleep(Duration::from_millis(25));
                }
            }
        });
    });
}

fn get_ai_info() -> Json {
    let mut enabled = false;
    let mut server = "https://192.168.1.107:8090".to_string();
    let mut wake_word = "Atri".to_string();
    let mut mode = "remote".to_string();
    let mut tts_voice = "anna".to_string();
    let mut tts_emotion = "neutral".to_string();
    if let Ok(txt) = fs::read_to_string(AI_CONFIG_FILE) {
        if let Ok(j) = Json::parse(&txt) {
            if let Some(b) = j.get("enabled").and_then(|v| v.as_bool()) { enabled = b; }
            if let Some(s) = j.get_str("server") { server = s.into(); }
            if let Some(s) = j.get_str("wake_word") { wake_word = s.into(); }
            if let Some(s) = j.get_str("mode") { mode = s.into(); }
            if let Some(s) = j.get_str("tts_voice") { tts_voice = s.into(); }
            if let Some(s) = j.get_str("tts_emotion") { tts_emotion = s.into(); }
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
        ("mode", Json::Str(mode)),
        ("tts_voice", Json::Str(tts_voice)),
        ("tts_emotion", Json::Str(tts_emotion)),
        ("state", Json::Str(state)),
        ("last_asr", Json::Str(last_asr)),
        ("last_answer", Json::Str(last_answer)),
        ("error", Json::Str(error)),
    ])
}

fn update_ai_json_fields(
    path: &Path,
    mode: Option<&str>,
    tts_voice: Option<&str>,
    tts_emotion: Option<&str>,
) {
    let mut map = fs::read_to_string(path)
        .ok()
        .and_then(|s| Json::parse(&s).ok())
        .and_then(|j| match j {
            Json::Obj(m) => Some(m),
            _ => None,
        })
        .unwrap_or_default();
    if let Some(m) = mode {
        map.insert("mode".into(), Json::Str(m.into()));
    }
    if let Some(v) = tts_voice {
        map.insert("tts_voice".into(), Json::Str(v.into()));
    }
    if let Some(e) = tts_emotion {
        map.insert("tts_emotion".into(), Json::Str(e.into()));
    }
    if let Some(p) = path.parent() {
        let _ = fs::create_dir_all(p);
    }
    let body = format!("{}
", Json::Obj(map).to_string_pretty());
    let _ = atomic_write_bytes(path, body.as_bytes(), 0o644, true);
}

pub(crate) fn valid_timezone_name(tz: &str) -> bool {
    !tz.is_empty()
        && tz.len() <= 64
        && !tz.starts_with('/')
        && !tz.ends_with('/')
        && !tz.contains("..")
        && tz
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'/' | b'_' | b'-' | b'+'))
}

pub(crate) fn timers_json_at(path: &Path, now: i64) -> Vec<Json> {
    let mut out = Vec::new();
    let Ok(txt) = fs::read_to_string(path) else {
        return out;
    };
    let Ok(Json::Arr(arr)) = Json::parse(&txt) else {
        return out;
    };
    for item in arr {
        let id = item.get("id").and_then(|v| v.as_i64()).unwrap_or(0).max(0);
        let fire_at = item.get("fire_at").and_then(|v| v.as_i64()).unwrap_or(0);
        let total_secs = item.get("total_secs").and_then(|v| v.as_i64()).unwrap_or(0).max(0);
        if fire_at > now {
            let remaining = (fire_at - now).max(0);
            out.push(obj(vec![
                ("id", Json::Num(id as f64)),
                ("fire_at", Json::Num(fire_at as f64)),
                ("total_secs", Json::Num(total_secs as f64)),
                ("remaining_secs", Json::Num(remaining as f64)),
            ]));
        }
    }
    out
}

pub(crate) fn save_timers_at(path: &Path, timers: &[Json]) {
    let clean: Vec<Json> = timers
        .iter()
        .map(|t| {
            obj(vec![
                ("id", Json::Num(t.get("id").and_then(|v| v.as_f64()).unwrap_or(0.0))),
                ("fire_at", Json::Num(t.get("fire_at").and_then(|v| v.as_f64()).unwrap_or(0.0))),
                ("total_secs", Json::Num(t.get("total_secs").and_then(|v| v.as_f64()).unwrap_or(0.0))),
            ])
        })
        .collect();
    if let Some(p) = path.parent() {
        let _ = fs::create_dir_all(p);
    }
    let body = Json::Arr(clean).to_string_pretty();
    let _ = atomic_write_bytes(path, body.as_bytes(), 0o644, false);
}

pub(crate) fn alarms_json_at(path: &Path) -> Vec<Json> {
    let mut out = Vec::new();
    let Ok(txt) = fs::read_to_string(path) else {
        return out;
    };
    let Ok(Json::Arr(arr)) = Json::parse(&txt) else {
        return out;
    };
    for item in arr {
        let id = item.get("id").and_then(|v| v.as_i64()).unwrap_or(0).max(0);
        let hour = item.get("hour").and_then(|v| v.as_i64()).unwrap_or(-1);
        let minute = item.get("minute").and_then(|v| v.as_i64()).unwrap_or(-1);
        let enabled = item.get("enabled").and_then(|v| v.as_bool()).unwrap_or(true);
        if (0..=23).contains(&hour) && (0..=59).contains(&minute) {
            out.push(obj(vec![
                ("id", Json::Num(id as f64)),
                ("hour", Json::Num(hour as f64)),
                ("minute", Json::Num(minute as f64)),
                ("enabled", Json::Bool(enabled)),
            ]));
        }
    }
    out
}

pub(crate) fn save_alarms_at(path: &Path, alarms: &[Json]) {
    let clean: Vec<Json> = alarms
        .iter()
        .map(|a| {
            obj(vec![
                ("id", Json::Num(a.get("id").and_then(|v| v.as_f64()).unwrap_or(0.0))),
                ("hour", Json::Num(a.get("hour").and_then(|v| v.as_f64()).unwrap_or(0.0))),
                ("minute", Json::Num(a.get("minute").and_then(|v| v.as_f64()).unwrap_or(0.0))),
                ("enabled", Json::Bool(a.get("enabled").and_then(|v| v.as_bool()).unwrap_or(true))),
                ("last_fired_day", Json::Num(0.0)),
            ])
        })
        .collect();
    if let Some(p) = path.parent() {
        let _ = fs::create_dir_all(p);
    }
    let body = Json::Arr(clean).to_string_pretty();
    let _ = atomic_write_bytes(path, body.as_bytes(), 0o644, true);
}

fn spawn_ota_bg(subcmd: &str, extra_args: &[&str]) -> Result<(), String> {
    let mut cmd = Command::new("/usr/lib/atrios/ota");
    cmd.arg(subcmd);
    for a in extra_args {
        cmd.arg(a);
    }
    cmd.stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("Failed to start OTA {subcmd}: {e}"))
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

fn read_eq_info() -> Json {
    if let Ok(txt) = fs::read_to_string("/var/lib/atrios/eq.json") {
        if let Ok(j) = Json::parse(&txt) {
            return j;
        }
    }
    obj(vec![
        ("enabled", Json::Bool(false)),
        ("preset", Json::Str("flat".into())),
        ("bands", Json::Arr(vec![Json::Num(0.0), Json::Num(0.0), Json::Num(0.0), Json::Num(0.0), Json::Num(0.0)])),
    ])
}

fn read_vqe_info() -> Json {
    if let Ok(txt) = fs::read_to_string("/var/run/atrios/aec_vqe.json") {
        if let Ok(j) = Json::parse(&txt) {
            return j;
        }
    }
    obj(vec![
        ("erle_db", Json::Num(0.0)),
        ("snr_db", Json::Num(0.0)),
        ("lag_ms", Json::Num(0.0)),
        ("best_mic", Json::Num(0.0)),
        ("barge_in", Json::Bool(false)),
        ("ref_active", Json::Bool(false)),
    ])
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
        ("vqe", read_vqe_info()),
        ("eq", read_eq_info()),
        ("ota", crate::ota::read_ota_status_json()),
        ("ymusic", crate::ymusic::read_status_json()),
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

fn shell_quote_single(s: &str) -> String {
    format!("'{}'", s.replace('\'', "'\"'\"'"))
}

pub(crate) fn process_command(cmd_obj: &Json) -> Result<Json, String> {
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
        "get_relay_info" => {
            let cfg = crate::atriai::load_config();
            Ok(obj(vec![
                ("type", Json::Str("relay_info".into())),
                ("device_id", Json::Str(device_relay_id())),
                ("gateway", Json::Str(cfg.server)),
                ("token", Json::Str(cfg.token.clone())),
                ("has_token", Json::Bool(!cfg.token.is_empty())),
            ]))
        }
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
        "set_eq" => {
            let enabled = map.get("enabled").and_then(|v| v.as_bool()).unwrap_or(true);
            let preset = map.get("preset").and_then(|v| v.as_str()).unwrap_or("custom").to_string();
            let mut bands = [0.0f64; 5];
            if let Some(arr) = map.get("bands").and_then(|v| v.as_arr()) {
                for (i, v) in arr.iter().take(5).enumerate() {
                    bands[i] = v.as_f64().unwrap_or(0.0).clamp(-12.0, 12.0);
                }
            }
            let eq_j = obj(vec![
                ("enabled", Json::Bool(enabled)),
                ("preset", Json::Str(preset)),
                ("bands", Json::Arr(bands.iter().map(|&b| Json::Num(b)).collect())),
            ]);
            let _ = fs::create_dir_all("/var/lib/atrios");
            let _ = atomic_write_bytes(
                Path::new("/var/lib/atrios/eq.json"),
                format!("{}\n", eq_j.to_string_compact()).as_bytes(),
                0o644,
                false,
            );
            Ok(obj(vec![
                ("type", Json::Str("eq_response".into())),
                ("success", Json::Bool(true)),
                ("eq", eq_j),
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
        "sync_time" | "ntp_sync" => {
            let _ = Command::new("/usr/bin/atrios")
                .args(["time", "sync"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            Ok(build_status_payload())
        }
        "set_timezone" => {
            let tz = map.get("tz").and_then(|v| v.as_str()).map(str::trim).unwrap_or("");
            if !valid_timezone_name(tz) {
                return Err("Invalid timezone".into());
            }
            let _ = Command::new("/usr/bin/atrios")
                .args(["time", "tz", tz])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            unsafe { tzset() };
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
                for pat in [
                    "/usr/lib/atrios/light screen",
                    "/usr/bin/atrios screen",
                    "/usr/lib/atrios/atrios-runtime screen",
                ] {
                    let _ = Command::new("pkill")
                        .args(["-9", "-f", pat])
                        .stdout(Stdio::null())
                        .stderr(Stdio::null())
                        .status();
                }
                if mode == "clock" {
                    let _ = fs::remove_file("/var/run/atrios/timer_display.json");
                }
                set_screen_mode(mode);
                if mode == "clock" {
                    // Immediately push a live clock frame so animation never stays frozen on /dev/fb0
                    let now_hm = crate::sys::format_local("%H:%M", None);
                    let buf = crate::light::render_clock_frame(&now_hm, crate::light::is_bt_active(), 200);
                    crate::light::screen_send_frame_raw(&buf);
                    // Ensure screend daemon is running to keep updating the clock every minute
                    let screend_alive = Command::new("pgrep")
                        .args(["-f", "/usr/lib/atrios/screend"])
                        .stdout(Stdio::null())
                        .stderr(Stdio::null())
                        .status()
                        .map(|s| s.success())
                        .unwrap_or(false);
                    if !screend_alive {
                        let _ = Command::new("/usr/lib/atrios/screend")
                            .stdin(Stdio::null())
                            .stdout(Stdio::null())
                            .stderr(Stdio::null())
                            .spawn();
                    }
                }
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
            let mode_opt = map
                .get("mode")
                .and_then(|v| v.as_str())
                .map(str::trim)
                .filter(|s| matches!(*s, "local" | "remote" | "hybrid"));
            let voice_opt = map
                .get("tts_voice")
                .and_then(|v| v.as_str())
                .map(str::trim)
                .filter(|s| !s.is_empty() && s.len() <= 64);
            let emo_opt = map
                .get("tts_emotion")
                .and_then(|v| v.as_str())
                .map(str::trim)
                .filter(|s| !s.is_empty() && s.len() <= 32);
            if mode_opt.is_some() || voice_opt.is_some() || emo_opt.is_some() {
                update_ai_json_fields(Path::new(AI_CONFIG_FILE), mode_opt, voice_opt, emo_opt);
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
            let secs = map.get("seconds").and_then(|v| v.as_i64()).unwrap_or(120).clamp(10, 300);
            let _ = Command::new("/usr/bin/atrios").args(["bt", "pair", &secs.to_string()]).status();
            Ok(build_status_payload())
        }
        "bt_speaker" => {
            let enabled = map.get("enabled").and_then(|v| v.as_bool()).unwrap_or(true);
            if enabled {
                let running = Command::new("pgrep")
                    .args(["-f", "/usr/sbin/bt-speaker-daemon"])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .status()
                    .map(|s| s.success())
                    .unwrap_or(false);
                if !running && Path::new("/usr/sbin/bt-speaker-daemon").exists() {
                    let _ = Command::new("/usr/sbin/bt-speaker-daemon")
                        .stdout(Stdio::null())
                        .stderr(Stdio::null())
                        .spawn();
                }
            } else {
                let _ = Command::new("pkill")
                    .args(["-f", "/usr/sbin/bt-speaker-daemon"])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .status();
                let _ = fs::create_dir_all("/var/run/atrios");
                let _ = fs::write(BT_ACTIVE_FILE, "0");
                crate::atriai::send_bt_cmd("speaker_off");
            }
            Ok(obj(vec![
                ("type", Json::Str("action_response".into())),
                ("success", Json::Bool(true)),
                ("is_speaker_mode", Json::Bool(enabled)),
                (
                    "message",
                    Json::Str(if enabled { "Bluetooth speaker enabled" } else { "Bluetooth speaker disabled" }.into()),
                ),
            ]))
        }
        "ymusic_status" => Ok(obj(vec![
            ("type", Json::Str("ymusic_status".into())),
            ("success", Json::Bool(true)),
            ("ymusic", crate::ymusic::read_status_json()),
        ])),
        "ymusic_login" => {
            let token = map.get("token").and_then(|v| v.as_str()).unwrap_or("");
            match crate::ymusic::login_with_token(token) {
                Ok(st) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_login_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str(format!("Signed in as {}", st.display_name))),
                    ("ymusic", st.to_json()),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_login_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                    ("ymusic", crate::ymusic::read_status_json()),
                ])),
            }
        }
        "ymusic_logout" => {
            let st = crate::ymusic::logout();
            Ok(obj(vec![
                ("type", Json::Str("ymusic_logout_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str("Logged out".into())),
                ("ymusic", st.to_json()),
            ]))
        }
        "ymusic_orbs" => {
            match crate::ymusic::fetch_orbs_json() {
                Ok(orbs) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_orbs_response".into())),
                    ("success", Json::Bool(true)),
                    ("orbs", Json::Arr(orbs)),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_orbs_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                    ("orbs", Json::Arr(Vec::new())),
                ])),
            }
        }
        "ymusic_collection" => {
            match crate::ymusic::fetch_collection_json() {
                Ok(col) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_collection_response".into())),
                    ("success", Json::Bool(true)),
                    ("collection", col),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_collection_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                ])),
            }
        }
        "ymusic_search" => {
            let q = map.get("query").and_then(|v| v.as_str()).unwrap_or("").trim();
            if q.is_empty() {
                Ok(obj(vec![
                    ("type", Json::Str("ymusic_search_response".into())),
                    ("success", Json::Bool(true)),
                    ("tracks", Json::Arr(Vec::new())),
                    ("albums", Json::Arr(Vec::new())),
                ]))
            } else {
                match crate::ymusic::search_catalog_json(q) {
                    Ok((tracks, albums)) => Ok(obj(vec![
                        ("type", Json::Str("ymusic_search_response".into())),
                        ("success", Json::Bool(true)),
                        ("query", Json::Str(q.into())),
                        ("tracks", Json::Arr(tracks)),
                        ("albums", Json::Arr(albums)),
                    ])),
                    Err(e) => Ok(obj(vec![
                        ("type", Json::Str("ymusic_search_response".into())),
                        ("success", Json::Bool(false)),
                        ("message", Json::Str(e)),
                        ("tracks", Json::Arr(Vec::new())),
                        ("albums", Json::Arr(Vec::new())),
                    ])),
                }
            }
        }
        "ymusic_play" => {
            let mode = map.get("mode").and_then(|v| v.as_str()).unwrap_or("wave");
            let query = map.get("query").and_then(|v| v.as_str());
            match crate::ymusic::trigger_play(mode, query) {
                Ok(st) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_play_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str(format!("Starting Yandex Music ({mode})"))),
                    ("ymusic", st.to_json()),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_play_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                    ("ymusic", crate::ymusic::read_status_json()),
                ])),
            }
        }
        "ymusic_control" => {
            let action = map.get("action").and_then(|v| v.as_str()).unwrap_or("");
            match crate::ymusic::trigger_control(action) {
                Ok(st) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_control_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str(format!("Yandex Music: {action}"))),
                    ("ymusic", st.to_json()),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ymusic_control_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                    ("ymusic", crate::ymusic::read_status_json()),
                ])),
            }
        }
        "ota_status" => Ok(obj(vec![
            ("type", Json::Str("ota_status".into())),
            ("success", Json::Bool(true)),
            ("ota", crate::ota::read_ota_status_json()),
        ])),
        "ota_check" => {
            if crate::ota::is_ota_busy() {
                return Ok(obj(vec![
                    ("type", Json::Str("ota_check_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str("OTA operation already in progress".into())),
                    ("ota", crate::ota::read_ota_status_json()),
                ]));
            }
            let ota = crate::ota::mark_ota_starting("checking", "checking", "Checking for firmware updates...");
            match spawn_ota_bg("check", &[]) {
                Ok(()) => Ok(obj(vec![
                    ("type", Json::Str("ota_check_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str("Update check started".into())),
                    ("ota", ota),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ota_check_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                    ("ota", crate::ota::read_ota_status_json()),
                ])),
            }
        }
        "ota_apply" => {
            if crate::ota::is_ota_busy() {
                return Ok(obj(vec![
                    ("type", Json::Str("ota_apply_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str("OTA operation already in progress".into())),
                    ("ota", crate::ota::read_ota_status_json()),
                ]));
            }
            let do_reboot = map.get("reboot").and_then(|v| v.as_bool()).unwrap_or(true);
            let ota = crate::ota::mark_ota_starting("downloading", "downloading", "Starting firmware update...");
            let args: &[&str] = if do_reboot { &["--reboot"] } else { &[] };
            match spawn_ota_bg("update", args) {
                Ok(()) => Ok(obj(vec![
                    ("type", Json::Str("ota_apply_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str("OTA update started".into())),
                    ("ota", ota),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ota_apply_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                    ("ota", crate::ota::read_ota_status_json()),
                ])),
            }
        }
        "ota_rollback" => {
            if !crate::ota::rollback_available() {
                return Ok(obj(vec![
                    ("type", Json::Str("ota_rollback_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str("No rollback snapshot available".into())),
                    ("ota", crate::ota::read_ota_status_json()),
                ]));
            }
            if crate::ota::is_ota_busy() {
                return Ok(obj(vec![
                    ("type", Json::Str("ota_rollback_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str("OTA operation already in progress".into())),
                    ("ota", crate::ota::read_ota_status_json()),
                ]));
            }
            let do_reboot = map.get("reboot").and_then(|v| v.as_bool()).unwrap_or(false);
            let ota = crate::ota::mark_ota_starting("rolling_back", "rolling_back", "Rolling back firmware...");
            let args: &[&str] = if do_reboot { &["--reboot"] } else { &[] };
            match spawn_ota_bg("rollback", args) {
                Ok(()) => Ok(obj(vec![
                    ("type", Json::Str("ota_rollback_response".into())),
                    ("success", Json::Bool(true)),
                    ("message", Json::Str("OTA rollback started".into())),
                    ("ota", ota),
                ])),
                Err(e) => Ok(obj(vec![
                    ("type", Json::Str("ota_rollback_response".into())),
                    ("success", Json::Bool(false)),
                    ("message", Json::Str(e)),
                    ("ota", crate::ota::read_ota_status_json()),
                ])),
            }
        }
        "get_timers" => {
            let timers = timers_json_at(Path::new(TIMERS_FILE), now_unix_i64());
            Ok(obj(vec![
                ("type", Json::Str("timers_response".into())),
                ("success", Json::Bool(true)),
                ("timers", Json::Arr(timers)),
            ]))
        }
        "set_timer" => {
            let Some(raw_secs) = map.get("secs").or_else(|| map.get("seconds")).and_then(|v| v.as_i64()) else {
                return Err("Missing timer secs".into());
            };
            if !(1..=86400).contains(&raw_secs) {
                return Err("Timer secs must be between 1 and 86400".into());
            }
            let now = now_unix_i64();
            let mut timers = timers_json_at(Path::new(TIMERS_FILE), now);
            let next_id = timers
                .iter()
                .filter_map(|t| t.get("id").and_then(|v| v.as_i64()))
                .max()
                .unwrap_or(0)
                + 1;
            timers.push(obj(vec![
                ("id", Json::Num(next_id as f64)),
                ("fire_at", Json::Num((now + raw_secs) as f64)),
                ("total_secs", Json::Num(raw_secs as f64)),
                ("remaining_secs", Json::Num(raw_secs as f64)),
            ]));
            save_timers_at(Path::new(TIMERS_FILE), &timers);
            let disp = obj(vec![
                ("timer_id", Json::Num(next_id as f64)),
                ("until", Json::Num((now + 5) as f64)),
                ("pinned", Json::Bool(false)),
            ]);
            let _ = fs::write("/var/run/atrios/timer_display.json", disp.to_string_compact());
            Ok(obj(vec![
                ("type", Json::Str("timers_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str("Timer set".into())),
                ("timers", Json::Arr(timers)),
            ]))
        }
        "cancel_timers" => {
            let now = now_unix_i64();
            let mut timers = timers_json_at(Path::new(TIMERS_FILE), now);
            if let Some(id) = map.get("id").and_then(|v| v.as_i64()) {
                timers.retain(|t| t.get("id").and_then(|v| v.as_i64()) != Some(id));
            } else {
                timers.clear();
            }
            save_timers_at(Path::new(TIMERS_FILE), &timers);
            if timers.is_empty() {
                let _ = fs::remove_file("/var/run/atrios/timer_display.json");
            }
            Ok(obj(vec![
                ("type", Json::Str("timers_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str("Timers cancelled".into())),
                ("timers", Json::Arr(timers)),
            ]))
        }
        "get_alarms" => {
            let alarms = alarms_json_at(Path::new(ALARMS_FILE));
            Ok(obj(vec![
                ("type", Json::Str("alarms_response".into())),
                ("success", Json::Bool(true)),
                ("alarms", Json::Arr(alarms)),
            ]))
        }
        "set_alarm" => {
            let Some(hour) = map.get("hour").and_then(|v| v.as_i64()) else {
                return Err("Missing alarm hour".into());
            };
            let Some(minute) = map.get("minute").and_then(|v| v.as_i64()) else {
                return Err("Missing alarm minute".into());
            };
            if !(0..=23).contains(&hour) || !(0..=59).contains(&minute) {
                return Err("Invalid alarm hour/minute".into());
            }
            let enabled = map.get("enabled").and_then(|v| v.as_bool()).unwrap_or(true);
            let mut alarms = alarms_json_at(Path::new(ALARMS_FILE));
            alarms.retain(|a| {
                !(a.get("hour").and_then(|v| v.as_i64()) == Some(hour)
                    && a.get("minute").and_then(|v| v.as_i64()) == Some(minute))
            });
            let next_id = alarms
                .iter()
                .filter_map(|a| a.get("id").and_then(|v| v.as_i64()))
                .max()
                .unwrap_or(0)
                + 1;
            alarms.push(obj(vec![
                ("id", Json::Num(next_id as f64)),
                ("hour", Json::Num(hour as f64)),
                ("minute", Json::Num(minute as f64)),
                ("enabled", Json::Bool(enabled)),
            ]));
            save_alarms_at(Path::new(ALARMS_FILE), &alarms);
            Ok(obj(vec![
                ("type", Json::Str("alarms_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str("Alarm set".into())),
                ("alarms", Json::Arr(alarms)),
            ]))
        }
        "cancel_alarms" => {
            let mut alarms = alarms_json_at(Path::new(ALARMS_FILE));
            if let Some(id) = map.get("id").and_then(|v| v.as_i64()) {
                alarms.retain(|a| a.get("id").and_then(|v| v.as_i64()) != Some(id));
            } else {
                alarms.clear();
            }
            save_alarms_at(Path::new(ALARMS_FILE), &alarms);
            Ok(obj(vec![
                ("type", Json::Str("alarms_response".into())),
                ("success", Json::Bool(true)),
                ("message", Json::Str("Alarms cancelled".into())),
                ("alarms", Json::Arr(alarms)),
            ]))
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
        "term_e2ee_init" => {
            use std::collections::HashMap;
            use std::sync::{Mutex, OnceLock};
            static E2EE_SESSIONS: OnceLock<Mutex<HashMap<String, ([u8; 32], i64)>>> = OnceLock::new();
            let store = E2EE_SESSIONS.get_or_init(|| Mutex::new(HashMap::new()));
            let mut priv_k = [0u8; 32];
            let mut sid_raw = [0u8; 8];
            crate::e2ee::random_bytes(&mut priv_k);
            crate::e2ee::random_bytes(&mut sid_raw);
            let pub_k = crate::e2ee::x25519_public_key(&priv_k);
            let sid = crate::e2ee::hex_encode(&sid_raw);
            let now = now_unix_i64();
            if let Ok(mut g) = store.lock() {
                g.retain(|_, (_, ts)| now - *ts < 1800);
                if g.len() >= 8 { g.clear(); }
                g.insert(sid.clone(), (priv_k, now));
            }
            // Also persist active E2EE session key in /var/run/atrios/term_e2ee.key (0600, tmpfs) so any handler thread sees it
            let _ = fs::create_dir_all("/var/run/atrios");
            let key_line = format!("{}:{}\n", sid, crate::e2ee::hex_encode(&priv_k));
            let _ = atomic_write_bytes(Path::new("/var/run/atrios/term_e2ee.key"), key_line.as_bytes(), 0o600, false);
            Ok(obj(vec![
                ("type", Json::Str("term_e2ee_init_response".into())),
                ("success", Json::Bool(true)),
                ("sid", Json::Str(sid)),
                ("pub", Json::Str(crate::e2ee::hex_encode(&pub_k))),
            ]))
        }
        "term_e2ee_exec" => {
            let sid = map.get("sid").and_then(|v| v.as_str()).unwrap_or("").trim();
            let peer_pub_hex = map.get("pub").and_then(|v| v.as_str()).unwrap_or("");
            let iv_hex = map.get("iv").and_then(|v| v.as_str()).unwrap_or("");
            let ct_hex = map.get("ct").and_then(|v| v.as_str()).unwrap_or("");
            let mac_hex = map.get("mac").and_then(|v| v.as_str()).unwrap_or("");
            let key_txt = fs::read_to_string("/var/run/atrios/term_e2ee.key").map_err(|_| "E2EE session not initialized")?;
            let (stored_sid, priv_hex) = key_txt.trim().split_once(':').ok_or("Invalid E2EE session state")?;
            if stored_sid != sid {
                return Err("E2EE session expired — reconnect terminal".into());
            }
            let priv_vec = crate::e2ee::hex_decode(priv_hex)?;
            let peer_vec = crate::e2ee::hex_decode(peer_pub_hex)?;
            let iv_vec = crate::e2ee::hex_decode(iv_hex)?;
            let ct_vec = crate::e2ee::hex_decode(ct_hex)?;
            let mac_vec = crate::e2ee::hex_decode(mac_hex)?;
            if priv_vec.len() != 32 || peer_vec.len() != 32 || iv_vec.len() != 12 || mac_vec.len() != 32 {
                return Err("Invalid E2EE field lengths".into());
            }
            let priv_k: [u8; 32] = priv_vec.try_into().unwrap();
            let peer_pub: [u8; 32] = peer_vec.try_into().unwrap();
            let iv: [u8; 12] = iv_vec.try_into().unwrap();
            let mac: [u8; 32] = mac_vec.try_into().unwrap();
            let shared = crate::e2ee::x25519(&priv_k, &peer_pub);
            let pt = crate::e2ee::decrypt_e2ee(&shared, &iv, &ct_vec, &mac)?;
            let shell_cmd = String::from_utf8_lossy(&pt).trim().to_string();
            if shell_cmd.is_empty() || shell_cmd.len() > 2048 {
                return Err("Empty or oversized command".into());
            }
            let out = Command::new("/bin/sh")
                .args(["-c", &format!("timeout 8 sh -c {} 2>&1 || true", shell_quote_single(&shell_cmd))])
                .output()
                .or_else(|_| Command::new("/bin/sh").args(["-c", &shell_cmd]).output())
                .map_err(|e| format!("Shell exec failed: {e}"))?;
            let mut combined = String::from_utf8_lossy(&out.stdout).to_string();
            if !out.stderr.is_empty() {
                if !combined.is_empty() && !combined.ends_with('\n') { combined.push('\n'); }
                combined.push_str(&String::from_utf8_lossy(&out.stderr));
            }
            if combined.len() > 14000 {
                combined.truncate(14000);
                combined.push_str("\n... [truncated]");
            }
            let mut resp_iv = [0u8; 12];
            crate::e2ee::random_bytes(&mut resp_iv);
            let (resp_ct, resp_mac) = crate::e2ee::encrypt_e2ee(&shared, &resp_iv, combined.as_bytes());
            Ok(obj(vec![
                ("type", Json::Str("term_e2ee_response".into())),
                ("success", Json::Bool(true)),
                ("sid", Json::Str(sid.into())),
                ("iv", Json::Str(crate::e2ee::hex_encode(&resp_iv))),
                ("ct", Json::Str(crate::e2ee::hex_encode(&resp_ct))),
                ("mac", Json::Str(crate::e2ee::hex_encode(&resp_mac))),
            ]))
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
    let tv = Timeval { tv_sec: 0, tv_usec: 150_000 };
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
    let mut last_ota_sig = {
        let o = crate::ota::read_ota_status();
        format!("{}:{}:{}", o.state, o.progress, o.updated_ts)
    };
    let mut last_ym_sig = {
        let y = crate::ymusic::read_status();
        format!("{}:{}:{}:{}:{}", y.state, y.track_id, y.liked, y.seq, y.updated_ms)
    };

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
        let cur_ota = crate::ota::read_ota_status();
        let cur_ota_sig = format!("{}:{}:{}", cur_ota.state, cur_ota.progress, cur_ota.updated_ts);
        if cur_ota_sig != last_ota_sig {
            last_ota_sig = cur_ota_sig;
            if !send_json_line(
                fd,
                &obj(vec![
                    ("type", Json::Str("ota_status".into())),
                    ("ota", cur_ota.to_json()),
                ]),
            ) {
                break;
            }
        }
        let cur_ym = crate::ymusic::read_status();
        let cur_ym_sig = format!("{}:{}:{}:{}:{}", cur_ym.state, cur_ym.track_id, cur_ym.liked, cur_ym.seq, cur_ym.updated_ms);
        if cur_ym_sig != last_ym_sig {
            last_ym_sig = cur_ym_sig;
            if !send_json_line(
                fd,
                &obj(vec![
                    ("type", Json::Str("ymusic_status".into())),
                    ("ymusic", crate::ymusic::read_status_json()),
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


fn device_relay_id() -> String {
    crate::hardware::device_identity().device_id
}

fn spawn_wifi_relay_worker() {
    thread::spawn(|| {
        let did = device_relay_id();
        println!("[wifi-relay] fast worker started (device_id={did})");
        let mut last_full_status = std::time::Instant::now() - Duration::from_secs(60);
        let mut cached_status = build_status_payload();
        loop {
            let cfg = crate::atriai::load_config();
            let base = cfg.server.trim().trim_end_matches('/').to_string();
            let token = cfg.token.trim().to_string();
            let ca_owned = if cfg.ca_file.is_empty() { None } else { Some(cfg.ca_file.clone()) };
            if !base.starts_with("https://") || token.is_empty() {
                thread::sleep(Duration::from_secs(5));
                continue;
            }
            if last_full_status.elapsed() >= Duration::from_secs(10) {
                cached_status = build_status_payload();
                last_full_status = std::time::Instant::now();
            } else if let Json::Obj(ref mut pairs) = cached_status {
                // Always inject live ymusic, volume/mute, and EQ (~0.1ms local reads) so relay NEVER serves stale track/vol/EQ!
                let live_ym = crate::ymusic::read_status_json();
                let live_eq = read_eq_info();
                let live_vol = get_current_volume();
                if let Some(entry) = pairs.iter_mut().find(|(k, _)| k.as_str() == "ymusic") {
                    *entry.1 = live_ym;
                }
                if let Some(entry) = pairs.iter_mut().find(|(k, _)| k.as_str() == "eq") {
                    *entry.1 = live_eq;
                }
                if let Some((_, Json::Obj(ref mut audio_pairs))) = pairs.iter_mut().find(|(k, _)| k.as_str() == "audio") {
                    if let Some(ve) = audio_pairs.iter_mut().find(|(k, _)| k.as_str() == "volume") {
                        *ve.1 = Json::Num(live_vol as f64);
                    }
                    if let Some(me) = audio_pairs.iter_mut().find(|(k, _)| k.as_str() == "mute") {
                        *me.1 = Json::Bool(live_vol == 0);
                    }
                }
            }
            let ident = crate::hardware::device_identity();
            let auth = format!("Bearer {token}");
            let hdrs = [
                ("Authorization", auth.as_str()),
                ("X-Atri-Device-Id", ident.device_id.as_str()),
                ("X-Atri-Device-Key", ident.device_key.as_str()),
                ("X-Atri-Hw-Hash", ident.hw_hash.as_str()),
            ];
            let poll_url = format!("{base}/relay/v1/device/{}/poll", ident.device_id);
            let payload = obj(vec![
                ("device_id", Json::Str(ident.device_id.clone())),
                ("device_key", Json::Str(ident.device_key.clone())),
                ("hw_hash", Json::Str(ident.hw_hash.clone())),
                ("status", cached_status.clone()),
            ]);
            match crate::https::request_json(&poll_url, Some(&payload), &hdrs, ca_owned.as_deref(), 25, 512 * 1024, None) {
                Ok(resp) => {
                    if let Some(cmd_id) = resp.get_str("cmd_id").filter(|s| !s.is_empty()).map(|s| s.to_string()) {
                        if let Some(cmd_payload) = resp.get("payload").cloned() {
                            let cmd_name = cmd_payload.get_str("cmd").unwrap_or("").to_string();
                            let is_fast_state_cmd = matches!(
                                cmd_name.as_str(),
                                "set_volume" | "volume" | "set_mute" | "mute" | "set_eq" | "ymusic_control" | "ymusic_play"
                            );
                            let base_c = base.clone();
                            let auth_c = auth.clone();
                            let ident_c = ident.clone();
                            let ca_c = ca_owned.clone();
                            if is_fast_state_cmd {
                                // Execute fast state mutation inline (~5-90ms) BEFORE re-entering /poll so the very next /poll pushes the NEW track/vol/EQ!
                                let reply = match process_command(&cmd_payload) {
                                    Ok(r) => r,
                                    Err(e) => obj(vec![
                                        ("type", Json::Str("error".into())),
                                        ("error", Json::Str(e)),
                                    ]),
                                };
                                thread::spawn(move || {
                                    let reply_url = format!("{base_c}/relay/v1/device/{}/reply", ident_c.device_id);
                                    let reply_body = obj(vec![
                                        ("cmd_id", Json::Str(cmd_id)),
                                        ("reply", reply),
                                    ]);
                                    let hdrs_c = [
                                        ("Authorization", auth_c.as_str()),
                                        ("X-Atri-Device-Id", ident_c.device_id.as_str()),
                                        ("X-Atri-Device-Key", ident_c.device_key.as_str()),
                                        ("X-Atri-Hw-Hash", ident_c.hw_hash.as_str()),
                                    ];
                                    let _ = crate::https::request_json(
                                        &reply_url,
                                        Some(&reply_body),
                                        &hdrs_c,
                                        ca_c.as_deref(),
                                        10,
                                        256 * 1024,
                                        None,
                                    );
                                });
                            } else {
                                // Heavy/network commands run on a background thread so /poll stays responsive
                                thread::spawn(move || {
                                    let reply = match process_command(&cmd_payload) {
                                        Ok(r) => r,
                                        Err(e) => obj(vec![
                                            ("type", Json::Str("error".into())),
                                            ("error", Json::Str(e)),
                                        ]),
                                    };
                                    let reply_url = format!("{base_c}/relay/v1/device/{}/reply", ident_c.device_id);
                                    let reply_body = obj(vec![
                                        ("cmd_id", Json::Str(cmd_id)),
                                        ("reply", reply),
                                    ]);
                                    let hdrs_c = [
                                        ("Authorization", auth_c.as_str()),
                                        ("X-Atri-Device-Id", ident_c.device_id.as_str()),
                                        ("X-Atri-Device-Key", ident_c.device_key.as_str()),
                                        ("X-Atri-Hw-Hash", ident_c.hw_hash.as_str()),
                                    ];
                                    let _ = crate::https::request_json(
                                        &reply_url,
                                        Some(&reply_body),
                                        &hdrs_c,
                                        ca_c.as_deref(),
                                        10,
                                        256 * 1024,
                                        None,
                                    );
                                });
                            }
                        }
                    }
                }
                Err(_) => {
                    thread::sleep(Duration::from_millis(500));
                }
            }
        }
    });
}

pub fn run(_args: &[String]) -> i32 {
    spawn_wifi_relay_worker();
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ble_ota_status_and_device_command_validation() {
        let resp = process_command(&Json::parse(r#"{"cmd":"ota_status"}"#).unwrap()).unwrap();
        assert_eq!(resp.get_str("type"), Some("ota_status"));
        assert_eq!(resp.get("success").and_then(|v| v.as_bool()), Some(true));
        let ota = resp.get("ota").expect("ota object present");
        assert!(ota.get_str("state").is_some());
        assert!(ota.get_str("current_version").is_some());
        assert!(ota.get("update_available").and_then(|v| v.as_bool()).is_some());

        let st = process_command(&Json::parse(r#"{"cmd":"get_status"}"#).unwrap()).unwrap();
        assert_eq!(st.get_str("type"), Some("status"));
        assert!(st.get("ota").is_some());
        assert!(st.get("ymusic").is_some());
        let ym_resp = process_command(&Json::parse(r#"{"cmd":"ymusic_status"}"#).unwrap()).unwrap();
        assert_eq!(ym_resp.get_str("type"), Some("ymusic_status"));
        assert_eq!(ym_resp.get("success").and_then(|v| v.as_bool()), Some(true));
        let ym = ym_resp.get("ymusic").expect("ymusic object present");
        assert!(ym.get_str("state").is_some());
        assert!(ym.get("logged_in").and_then(|v| v.as_bool()).is_some());
        let ai = st.get("ai").expect("ai object present");
        assert!(ai.get_str("mode").is_some());
        assert!(ai.get_str("tts_voice").is_some());
        assert!(ai.get_str("tts_emotion").is_some());

        assert!(valid_timezone_name("Europe/Moscow"));
        assert!(valid_timezone_name("UTC"));
        assert!(valid_timezone_name("Etc/GMT+3"));
        assert!(!valid_timezone_name("../etc/passwd"));
        assert!(!valid_timezone_name("/Europe/Moscow"));
        assert!(!valid_timezone_name(""));

        assert!(process_command(&Json::parse(r#"{"cmd":"set_timezone","tz":"../bad"}"#).unwrap()).is_err());
        assert!(process_command(&Json::parse(r#"{"cmd":"set_timer","secs":0}"#).unwrap()).is_err());
        assert!(process_command(&Json::parse(r#"{"cmd":"set_alarm","hour":25,"minute":0}"#).unwrap()).is_err());
    }

    #[test]
    fn timers_alarms_and_ai_fields_persistence() {
        let dir = std::env::temp_dir().join(format!("atrios-bt-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();

        let tfile = dir.join("timers.json");
        let now = 1_700_000_000i64;
        let tlist = vec![obj(vec![
            ("id", Json::Num(1.0)),
            ("fire_at", Json::Num((now + 300) as f64)),
            ("total_secs", Json::Num(300.0)),
        ])];
        save_timers_at(&tfile, &tlist);
        let loaded_t = timers_json_at(&tfile, now + 10);
        assert_eq!(loaded_t.len(), 1);
        assert_eq!(loaded_t[0].get("remaining_secs").and_then(|v| v.as_i64()), Some(290));

        let afile = dir.join("alarms.json");
        let alist = vec![obj(vec![
            ("id", Json::Num(1.0)),
            ("hour", Json::Num(7.0)),
            ("minute", Json::Num(30.0)),
            ("enabled", Json::Bool(true)),
        ])];
        save_alarms_at(&afile, &alist);
        let loaded_a = alarms_json_at(&afile);
        assert_eq!(loaded_a.len(), 1);
        assert_eq!(loaded_a[0].get("hour").and_then(|v| v.as_i64()), Some(7));
        assert_eq!(loaded_a[0].get("minute").and_then(|v| v.as_i64()), Some(30));

        let aifile = dir.join("ai.json");
        update_ai_json_fields(&aifile, Some("hybrid"), Some("serena"), Some("happy"));
        let parsed = Json::parse(&fs::read_to_string(&aifile).unwrap()).unwrap();
        assert_eq!(parsed.get_str("mode"), Some("hybrid"));
        assert_eq!(parsed.get_str("tts_voice"), Some("serena"));
        assert_eq!(parsed.get_str("tts_emotion"), Some("happy"));

        let _ = fs::remove_dir_all(&dir);
    }
}
