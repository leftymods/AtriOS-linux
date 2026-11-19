use std::ffi::CString;
use std::fs::{self, OpenOptions};
use std::io::{Read, Write};
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::os::raw::{c_int, c_void};
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::AsRawFd;
use std::os::unix::net::UnixStream;
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;

use crate::sys::{
    atomic_write_bytes, cfmakeraw, close, format_local, format_utc, geteuid, grantpt, if_nametoindex,
    ioctl, now_unix_i64, now_unix_secs, poll, posix_openpt, ptsname_r, recv, recv_or_read, send,
    setsockopt, signal, socket, tcflush, tcgetattr, tcsetattr, unlockpt, write_fd, PollFd,
    SockAddrNl, Termios, Timespec, AF_NETLINK, B115200, B3000000, CLOCK_REALTIME, CLOCAL, CREAD,
    CRTSCTS, CS8, HCIUARTSETPROTO, NETLINK_ADD_MEMBERSHIP, NETLINK_GENERIC, PARENB, PARODD,
    POLLIN, SIGINT, SIGTERM, SOL_NETLINK, TCIOFLUSH, TCSANOW, TIOCSETD, VMIN, VTIME,
};

// =====================================================================
// 1. NTP (SNTP UDP/123 + RTC + timezone)
// =====================================================================
const NTP_DELTA: u64 = 2_208_988_800;
const NTP_SERVERS: &[&str] = &[
    "ru.pool.ntp.org",
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com",
    "ntp1.stratum2.ru",
];

fn sntp_query(host: &str) -> Result<f64, String> {
    let mut pkt = [0u8; 48];
    pkt[0] = 0x1b;
    let addrs: Vec<SocketAddr> = format!("{host}:123")
        .to_socket_addrs()
        .map_err(|e| e.to_string())?
        .filter(|a| a.is_ipv4())
        .collect();
    let addr = *addrs.first().ok_or("No IPv4 address for NTP host")?;
    let sock = UdpSocket::bind("0.0.0.0:0").map_err(|e| e.to_string())?;
    let _ = sock.set_read_timeout(Some(Duration::from_secs(3)));
    let _ = sock.set_write_timeout(Some(Duration::from_secs(3)));
    let t0 = now_unix_secs();
    sock.send_to(&pkt, addr).map_err(|e| e.to_string())?;
    let mut resp = [0u8; 1024];
    let (n, _) = sock.recv_from(&mut resp).map_err(|e| e.to_string())?;
    let t1 = now_unix_secs();
    if n < 48 {
        return Err("short NTP response".into());
    }
    let sec = u32::from_be_bytes(resp[40..44].try_into().unwrap()) as u64;
    let frac = u32::from_be_bytes(resp[44..48].try_into().unwrap()) as f64;
    if sec <= NTP_DELTA {
        return Err("invalid NTP timestamp".into());
    }
    let rtt = (t1 - t0).max(0.0);
    Ok(((sec - NTP_DELTA) as f64) + (frac / 4_294_967_296.0) + rtt / 2.0)
}

fn set_system_time(unix_ts: f64) {
    let secs = unix_ts.floor() as i64;
    let nsec = ((unix_ts - (secs as f64)) * 1e9) as i64;
    let ts = Timespec { tv_sec: secs, tv_nsec: nsec.clamp(0, 999_999_999) };
    let rc = unsafe { crate::sys::clock_settime(CLOCK_REALTIME, &ts) };
    if rc != 0 {
        let t_str = format_utc("%Y-%m-%d %H:%M:%S", secs);
        let _ = Command::new("date").args(["-u", "-s", &t_str]).status();
    }
    for hw in ["/sbin/hwclock", "/usr/sbin/hwclock"] {
        if Path::new(hw).exists() {
            let _ = Command::new(hw)
                .args(["-w", "-u"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            break;
        }
    }
}

fn ntp_sync_once(quiet: bool) -> bool {
    let mut last_err = String::from("no NTP servers");
    for srv in NTP_SERVERS {
        match sntp_query(srv) {
            Ok(ts) => {
                set_system_time(ts);
                if !quiet {
                    println!(
                        "[atrios-ntp] Synced via {srv}: {}",
                        format_local("%Y-%m-%d %H:%M:%S %Z", None)
                    );
                }
                return true;
            }
            Err(e) => last_err = e,
        }
    }
    if !quiet {
        eprintln!("[atrios-ntp] Sync failed: {last_err}");
    }
    false
}

pub fn run_ntp(args: &[String]) -> i32 {
    let cmd = args.first().map(String::as_str).unwrap_or("status");
    match cmd {
        "sync" | "now" => {
            if ntp_sync_once(false) { 0 } else { 1 }
        }
        "daemon" => {
            if now_unix_i64() < 1_600_000_000 {
                for hw in ["/sbin/hwclock", "/usr/sbin/hwclock"] {
                    if Path::new(hw).exists() {
                        let _ = Command::new(hw)
                            .args(["-s", "-u"])
                            .stdout(Stdio::null())
                            .stderr(Stdio::null())
                            .status();
                        break;
                    }
                }
            }
            loop {
                if ntp_sync_once(false) {
                    thread::sleep(Duration::from_secs(6 * 3600));
                } else {
                    thread::sleep(Duration::from_secs(10));
                }
            }
        }
        "tz" | "timezone" if args.len() > 1 => {
            let tz_name = &args[1];
            let zone_dir = Path::new("/usr/share/zoneinfo");
            let Ok(base) = fs::canonicalize(zone_dir) else {
                eprintln!("Zoneinfo missing");
                return 1;
            };
            let Ok(target) = fs::canonicalize(zone_dir.join(tz_name)) else {
                eprintln!("Error: timezone '{tz_name}' not found in /usr/share/zoneinfo");
                return 1;
            };
            if !target.starts_with(&base) || !target.is_file() {
                eprintln!("Error: invalid timezone '{tz_name}'");
                return 1;
            }
            let _ = fs::remove_file("/etc/localtime");
            if let Err(e) = std::os::unix::fs::symlink(&target, "/etc/localtime") {
                eprintln!("Error setting timezone: {e}");
                return 1;
            }
            let _ = fs::write("/etc/timezone", format!("{tz_name}\n"));
            println!(
                "[atrios-ntp] Timezone set to {tz_name} ({})",
                format_local("%Y-%m-%d %H:%M:%S %Z", None)
            );
            0
        }
        "status" => {
            let mut tz = "UTC".to_string();
            if let Ok(s) = fs::read_to_string("/etc/timezone") {
                if !s.trim().is_empty() {
                    tz = s.trim().into();
                }
            } else if let Ok(p) = fs::read_link("/etc/localtime") {
                let s = p.to_string_lossy().into_owned();
                tz = s.replace("/usr/share/zoneinfo/", "");
            }
            let now = now_unix_i64();
            println!("Local time : {}", format_local("%Y-%m-%d %H:%M:%S %Z (%z)", Some(now)));
            println!("UTC time   : {} UTC", format_utc("%Y-%m-%d %H:%M:%S", now));
            println!("Timezone   : {tz}");
            0
        }
        _ => {
            println!("Usage: atrios time [status|sync|tz <Region/City>|daemon]");
            1
        }
    }
}

// =====================================================================
// 2. MICPROBE
// =====================================================================
pub fn run_micprobe(_args: &[String]) -> i32 {
    const CARD: &str = "AMLAUGESOUND";
    const TMP: &str = "/tmp/atrios-micprobe.wav";
    let amixer = |args: &[&str]| {
        let _ = Command::new("amixer")
            .arg("-c")
            .arg(CARD)
            .args(args)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
    };
    println!("AtriOS microphone path probe");
    println!("{}", "=".repeat(60));
    amixer(&["cset", "name=Audio Out Sink", "1"]);
    for i in 1..=4 {
        amixer(&["cset", &format!("name=Loopback ADC{i}_MUTE"), "0"]);
    }
    for (n, v) in [
        ("Loopback ADC1_MAX_GAIN", "188"),
        ("Loopback ADC2_MAX_GAIN", "203"),
        ("Loopback ADC3_MAX_GAIN", "191"),
        ("Loopback ADC4_MAX_GAIN", "191"),
    ] {
        amixer(&["cset", &format!("name={n}"), v]);
    }
    let sources = [
        ("LOOPBACK_A", "7"),
        ("LOOPBACK_B", "9"),
        ("TDMIN_LB", "6"),
        ("TDMIN_B", "1"),
        ("PDMIN", "4"),
    ];
    let mut found = Vec::new();
    for (name, idx) in sources {
        amixer(&["cset", "name=Audio In Source", name]);
        amixer(&["cset", "name=Audio In Source", idx]);
        for dev in [0, 3, 1] {
            let _ = fs::remove_file(TMP);
            let mut recorded = false;
            for c in [CARD, "0"] {
                if Command::new("arecord")
                    .args([
                        "-q",
                        "-D",
                        &format!("hw:{c},{dev}"),
                        "-c",
                        "2",
                        "-r",
                        "48000",
                        "-f",
                        "S32_LE",
                        "-d",
                        "1",
                        TMP,
                    ])
                    .stdout(Stdio::null())
                    .stderr(Stdio::null())
                    .status()
                    .map(|s| s.success())
                    .unwrap_or(false)
                    && fs::metadata(TMP).map(|m| m.len() > 44).unwrap_or(false)
                {
                    recorded = true;
                    break;
                }
            }
            if !recorded {
                println!("source={name:10} dev={dev}  capture failed");
                continue;
            }
            let Ok(data) = fs::read(TMP) else { continue };
            if data.len() < 48 { continue; }
            let ch = u16::from_le_bytes([data[22], data[23]]) as usize;
            let bits = u16::from_le_bytes([data[34], data[35]]);
            if bits != 32 || ch == 0 {
                println!("source={name:10} dev={dev}  unsupported format");
                continue;
            }
            let mut pos = 12usize;
            let mut payload = &data[44..];
            while pos + 8 <= data.len() {
                let sz = u32::from_le_bytes(data[pos + 4..pos + 8].try_into().unwrap()) as usize;
                if &data[pos..pos + 4] == b"data" && pos + 8 + sz <= data.len() {
                    payload = &data[pos + 8..pos + 8 + sz];
                    break;
                }
                pos += 8 + sz + (sz & 1);
            }
            let n_samples = payload.len() / 4;
            let mut left = Vec::with_capacity(n_samples / ch + 1);
            for i in (0..n_samples).step_by(ch) {
                left.push(i32::from_le_bytes(payload[i * 4..i * 4 + 4].try_into().unwrap()));
            }
            if left.is_empty() { continue; }
            let peak = left.iter().map(|&x| (x as i64).abs()).max().unwrap_or(0) as f64 / 2147483648.0;
            let rms = (left.iter().map(|&x| (x as f64) * (x as f64)).sum::<f64>() / left.len() as f64).sqrt() / 2147483648.0;
            let zc = left.windows(2).filter(|w| (w[0] >= 0) != (w[1] >= 0)).count();
            let zcr = (zc as f64) / ((left.len().saturating_sub(1)).max(1) as f64);
            println!("source={name:10} dev={dev}  rms={rms:.5} peak={peak:.5} zcr={zcr:.4}");
            if rms > 0.001 && peak > 0.01 {
                found.push((name, dev, rms, peak, zcr));
            }
        }
    }
    println!("{}", "=".repeat(60));
    if found.is_empty() {
        println!("No combination produced a signal above the noise floor.");
    } else {
        println!("Candidates with real signal:");
        for (name, dev, rms, peak, zcr) in found {
            println!("  source={name} dev={dev} rms={rms:.5} peak={peak:.5} zcr={zcr:.4}");
        }
    }
    0
}

// =====================================================================
// 3. Minimal D-Bus UNIX wire codec (used by bt-agent & bt-volume-sync & bt-control)
// =====================================================================
fn dbus_pad(buf: &mut Vec<u8>, align: usize) {
    while buf.len() % align != 0 {
        buf.push(0);
    }
}

fn dbus_enc_str(buf: &mut Vec<u8>, s: &str) {
    dbus_pad(buf, 4);
    buf.extend_from_slice(&(s.len() as u32).to_le_bytes());
    buf.extend_from_slice(s.as_bytes());
    buf.push(0);
}

fn dbus_make_header(
    msg_type: u8,
    serial: u32,
    fields: &[(u8, &str, Vec<u8>)],
    body_len: usize,
) -> Vec<u8> {
    let mut fb = Vec::new();
    for (code, sig, val) in fields {
        dbus_pad(&mut fb, 8);
        fb.push(*code);
        fb.push(sig.len() as u8);
        fb.extend_from_slice(sig.as_bytes());
        fb.push(0);
        if matches!(*sig, "s" | "o" | "u") {
            dbus_pad(&mut fb, 4);
        }
        fb.extend_from_slice(val);
    }
    let mut hdr = Vec::with_capacity(16 + fb.len() + 8);
    hdr.push(b'l');
    hdr.push(msg_type);
    hdr.push(0);
    hdr.push(1);
    hdr.extend_from_slice(&(body_len as u32).to_le_bytes());
    hdr.extend_from_slice(&serial.to_le_bytes());
    hdr.extend_from_slice(&(fb.len() as u32).to_le_bytes());
    hdr.extend_from_slice(&fb);
    dbus_pad(&mut hdr, 8);
    hdr
}

#[allow(dead_code)]
struct DbusMsg {
    mtype: u8,
    serial: u32,
    interface: String,
    member: String,
    sender: String,
    body: Vec<u8>,
}

struct DbusConn {
    sock: UnixStream,
    serial: u32,
}

impl DbusConn {
    fn connect() -> Result<Self, String> {
        let mut sock = UnixStream::connect("/var/run/dbus/system_bus_socket").map_err(|e| e.to_string())?;
        let uid_str = unsafe { geteuid() }.to_string();
        let uid_hex = crate::sys::hex_lower(uid_str.as_bytes());
        sock.write_all(format!("\0AUTH EXTERNAL {uid_hex}\r\n").as_bytes())
            .map_err(|e| e.to_string())?;
        let mut line = [0u8; 256];
        let n = sock.read(&mut line).map_err(|e| e.to_string())?;
        if !line[..n].starts_with(b"OK") {
            return Err("DBus AUTH EXTERNAL failed".into());
        }
        sock.write_all(b"BEGIN\r\n").map_err(|e| e.to_string())?;
        let mut c = Self { sock, serial: 1 };
        let _ = c.call(
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "org.freedesktop.DBus",
            "Hello",
            None,
            &[],
        )?;
        Ok(c)
    }

    fn read_msg(&mut self) -> Result<DbusMsg, String> {
        let mut raw_hdr = [0u8; 16];
        self.sock.read_exact(&mut raw_hdr).map_err(|e| e.to_string())?;
        let mtype = raw_hdr[1];
        let body_len = u32::from_le_bytes(raw_hdr[4..8].try_into().unwrap()) as usize;
        let serial = u32::from_le_bytes(raw_hdr[8..12].try_into().unwrap());
        let fields_len = u32::from_le_bytes(raw_hdr[12..16].try_into().unwrap()) as usize;
        let padded_fields = (fields_len + 7) & !7;
        let mut fbuf = vec![0u8; padded_fields];
        self.sock.read_exact(&mut fbuf).map_err(|e| e.to_string())?;
        let mut body = vec![0u8; body_len];
        self.sock.read_exact(&mut body).map_err(|e| e.to_string())?;

        let mut interface = String::new();
        let mut member = String::new();
        let mut sender = String::new();
        let mut idx = 0usize;
        while idx < fields_len {
            idx = (idx + 7) & !7;
            if idx + 2 >= fields_len { break; }
            let code = fbuf[idx];
            let slen = fbuf[idx + 1] as usize;
            idx += 2;
            if idx + slen + 1 > fields_len { break; }
            let sig = &fbuf[idx..idx + slen];
            idx += slen + 1;
            if sig == b"s" || sig == b"o" {
                idx = (idx + 3) & !3;
                if idx + 4 > fields_len { break; }
                let vlen = u32::from_le_bytes(fbuf[idx..idx + 4].try_into().unwrap()) as usize;
                idx += 4;
                if idx + vlen + 1 > fbuf.len() { break; }
                let s = String::from_utf8_lossy(&fbuf[idx..idx + vlen]).into_owned();
                idx += vlen + 1;
                match code {
                    2 => interface = s,
                    3 => member = s,
                    7 => sender = s,
                    _ => {}
                }
            } else if sig == b"u" {
                idx = (idx + 3) & !3;
                idx += 4;
            } else if sig == b"g" {
                if idx >= fields_len { break; }
                let gl = fbuf[idx] as usize;
                idx += 1 + gl + 1;
            } else {
                break;
            }
        }
        Ok(DbusMsg { mtype, serial, interface, member, sender, body })
    }

    fn call(
        &mut self,
        path: &str,
        dest: &str,
        iface: &str,
        method: &str,
        sig: Option<&str>,
        body: &[u8],
    ) -> Result<DbusMsg, String> {
        let mut v_path = Vec::new();
        dbus_enc_str(&mut v_path, path);
        let mut v_dest = Vec::new();
        dbus_enc_str(&mut v_dest, dest);
        let mut v_if = Vec::new();
        dbus_enc_str(&mut v_if, iface);
        let mut v_meth = Vec::new();
        dbus_enc_str(&mut v_meth, method);
        let mut fields = vec![
            (1u8, "o", v_path),
            (6, "s", v_dest),
            (2, "s", v_if),
            (3, "s", v_meth),
        ];
        if let Some(sg) = sig {
            let mut v_sig = Vec::new();
            v_sig.push(sg.len() as u8);
            v_sig.extend_from_slice(sg.as_bytes());
            v_sig.push(0);
            fields.push((8, "g", v_sig));
        }
        let ser = self.serial;
        self.serial += 1;
        let hdr = dbus_make_header(1, ser, &fields, body.len());
        self.sock.write_all(&hdr).map_err(|e| e.to_string())?;
        if !body.is_empty() {
            self.sock.write_all(body).map_err(|e| e.to_string())?;
        }
        self.read_msg()
    }

    fn send_method_return(&mut self, dest: &str, reply_serial: u32) {
        let mut v_dest = Vec::new();
        dbus_enc_str(&mut v_dest, dest);
        let fields = vec![
            (6u8, "s", v_dest),
            (5u8, "u", reply_serial.to_le_bytes().to_vec()),
        ];
        let ser = self.serial;
        self.serial += 1;
        let hdr = dbus_make_header(2, ser, &fields, 0);
        let _ = self.sock.write_all(&hdr);
    }

    fn set_prop_bool(&mut self, path: &str, dest: &str, iface: &str, prop: &str, val: bool) {
        let mut body = Vec::new();
        dbus_enc_str(&mut body, iface);
        dbus_enc_str(&mut body, prop);
        body.extend_from_slice(b"\x01b\x00");
        dbus_pad(&mut body, 4);
        body.extend_from_slice(&(if val { 1u32 } else { 0 }).to_le_bytes());
        let _ = self.call(
            path,
            dest,
            "org.freedesktop.DBus.Properties",
            "Set",
            Some("ssv"),
            &body,
        );
    }

    fn set_prop_u16(&mut self, path: &str, dest: &str, iface: &str, prop: &str, val: u16) {
        let mut body = Vec::new();
        dbus_enc_str(&mut body, iface);
        dbus_enc_str(&mut body, prop);
        body.extend_from_slice(b"\x01q\x00");
        dbus_pad(&mut body, 2);
        body.extend_from_slice(&val.to_le_bytes());
        let _ = self.call(
            path,
            dest,
            "org.freedesktop.DBus.Properties",
            "Set",
            Some("ssv"),
            &body,
        );
    }
}

fn extract_ascii_paths(data: &[u8], prefix: &str) -> Vec<String> {
    let mut out = Vec::new();
    let pb = prefix.as_bytes();
    let mut i = 0;
    while i + pb.len() < data.len() {
        if &data[i..i + pb.len()] == pb {
            let mut end = i + pb.len();
            while end < data.len()
                && (data[end].is_ascii_alphanumeric() || matches!(data[end], b'/' | b'_' | b'-' | b':'))
            {
                end += 1;
            }
            if let Ok(s) = std::str::from_utf8(&data[i..end]) {
                if !out.iter().any(|x| x == s) {
                    out.push(s.to_string());
                }
            }
            i = end;
        } else {
            i += 1;
        }
    }
    out
}

pub fn dbus_bluez_transports() -> Vec<String> {
    let Ok(mut conn) = DbusConn::connect() else { return Vec::new() };
    let Ok(reply) = conn.call(
        "/",
        "org.bluez",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        None,
        &[],
    ) else { return Vec::new() };
    extract_ascii_paths(&reply.body, "/org/bluez/hci0/dev_")
        .into_iter()
        .filter(|s| s.contains("/fd"))
        .collect()
}

pub fn dbus_bluealsa_pcms() -> Vec<String> {
    let Ok(mut conn) = DbusConn::connect() else { return Vec::new() };
    let Ok(reply) = conn.call(
        "/org/bluealsa",
        "org.bluealsa",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        None,
        &[],
    ) else { return Vec::new() };
    if reply.mtype == 3 {
        return Vec::new();
    }
    extract_ascii_paths(&reply.body, "/org/bluealsa/")
}

// =====================================================================
// 4. BT-AGENT
// =====================================================================
pub fn run_bt_agent(_args: &[String]) -> i32 {
    let Ok(mut conn) = DbusConn::connect() else {
        eprintln!("[bt-agent] Failed connecting to system D-Bus");
        return 1;
    };
    let mut b1 = Vec::new();
    dbus_enc_str(&mut b1, "/atrios/agent");
    dbus_enc_str(&mut b1, "NoInputNoOutput");
    let _ = conn.call("/org/bluez", "org.bluez", "org.bluez.AgentManager1", "RegisterAgent", Some("os"), &b1);

    let mut b2 = Vec::new();
    dbus_enc_str(&mut b2, "/atrios/agent");
    let _ = conn.call("/org/bluez", "org.bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", Some("o"), &b2);
    println!("[bt-agent] Agent running and auto-accepting pairing...");

    let fd = conn.sock.as_raw_fd();
    loop {
        let mut pfd = PollFd { fd, events: POLLIN, revents: 0 };
        let rc = unsafe { poll(&mut pfd, 1, 5000) };
        if rc > 0 && (pfd.revents & POLLIN) != 0 {
            let Ok(msg) = conn.read_msg() else { break };
            if msg.mtype == 1 {
                let sender = if msg.sender.is_empty() { "org.bluez".into() } else { msg.sender };
                println!("[bt-agent] Auto-confirming {} from {}", msg.member, sender);
                conn.send_method_return(&sender, msg.serial);
                if msg.body.len() >= 5 {
                    let plen = u32::from_le_bytes(msg.body[0..4].try_into().unwrap()) as usize;
                    if plen > 0 && 4 + plen <= msg.body.len() {
                        let dev_path = String::from_utf8_lossy(&msg.body[4..4 + plen]).into_owned();
                        if dev_path.starts_with("/org/bluez/") && dev_path.contains("/dev_") {
                            println!("[bt-agent] Trusting {dev_path}");
                            conn.set_prop_bool(&dev_path, "org.bluez", "org.freedesktop.DBus.Properties", "Trusted", true);
                        }
                    }
                }
            }
        }
    }
    0
}

// =====================================================================
// 5. BT-VOLUME-SYNC
// =====================================================================
fn bt_vol_to_raw(pct: i32, max_val: i32) -> i32 {
    let pct = pct.clamp(0, 100);
    if pct <= 0 { return 0; }
    if pct >= 100 { return max_val; }
    let max_db = (max_val as f64) * 0.5 - 12.0;
    let db = -max_db * (1.0 - (pct as f64 / 100.0).sqrt());
    (((max_val as f64) + 2.0 * db).round() as i32).clamp(1, max_val)
}

fn bt_apply_hw_volume(pct: i32) {
    const STATE_FILE: &str = "/var/lib/atrios/volume";
    let r255 = bt_vol_to_raw(pct, 255);
    let rhp = bt_vol_to_raw(pct, 191);
    let sw = if pct <= 0 { 0 } else { 1 };
    let batch = format!(
        "cset name='Woofer Master Playback Volume' {r255}\n\
         cset name='Tweeters Master Playback Volume' {r255}\n\
         cset name='Headphone DAC Playback Volume' {rhp}\n\
         cset name='Woofer Master Playback Switch' {sw}\n\
         cset name='Tweeters Master Playback Switch' {sw}\n\
         cset name='Headphone Headphone Switch' {sw}\n\
         cset name='Audio Out Sink' 1\n\
         cset name='Woofer Ch1 Playback Switch' 1\n\
         cset name='Woofer Ch2 Playback Switch' 1\n\
         cset name='Tweeters Ch1 Playback Switch' 1\n\
         cset name='Tweeters Ch2 Playback Switch' 1\n\
         cset name='Woofer Ch1 Playback Volume' 159\n\
         cset name='Woofer Ch2 Playback Volume' 159\n\
         cset name='Tweeters Ch1 Playback Volume' 159\n\
         cset name='Tweeters Ch2 Playback Volume' 159\n\
         cset name='SPK mute' 0\n"
    );
    if let Ok(mut child) = Command::new("amixer")
        .args(["-c", "AMLAUGESOUND", "-q", "-s"])
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    {
        if let Some(mut sin) = child.stdin.take() {
            let _ = sin.write_all(batch.as_bytes());
        }
        let _ = child.wait();
    }
    let _ = atomic_write_bytes(Path::new(STATE_FILE), format!("{pct}\n").as_bytes(), 0o644, false);
}

pub fn run_bt_volume_sync(_args: &[String]) -> i32 {
    const STATE_FILE: &str = "/var/lib/atrios/volume";
    let read_vol = || -> i32 {
        fs::read_to_string(STATE_FILE)
            .ok()
            .and_then(|s| s.trim().parse::<i32>().ok())
            .map(|v| v.clamp(0, 100))
            .unwrap_or(60)
    };

    let init_vol = read_vol();
    bt_apply_hw_volume(init_vol);

    let shared = std::sync::Arc::new(std::sync::Mutex::new((init_vol, 0.0f64, String::new())));
    let s1 = std::sync::Arc::clone(&shared);
    thread::spawn(move || loop {
        if let Ok(mut child) = Command::new("dbus-monitor")
            .args(["--system", "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"])
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
        {
            if let Some(out) = child.stdout.take() {
                let mut reader = std::io::BufReader::new(out);
                let mut line = String::new();
                let mut in_vol = false;
                while std::io::BufRead::read_line(&mut reader, &mut line).unwrap_or(0) > 0 {
                    let tr = line.trim();
                    if tr.contains("string \"Volume\"") {
                        in_vol = true;
                    } else if in_vol && tr.contains("uint16") {
                        if let Some(val) = tr.split_whitespace().last().and_then(|x| x.parse::<i32>().ok()) {
                            let pct = if val > 127 {
                                ((((val >> 8) & 0xff) as f64) * 100.0 / 127.0).round() as i32
                            } else {
                                ((val as f64) * 100.0 / 127.0).round() as i32
                            };
                            let mut g = s1.lock().unwrap();
                            let now = now_unix_secs();
                            if (pct - g.0).abs() > 1 && !(g.2 == "station" && now - g.1 < 0.6) {
                                g.0 = pct;
                                g.1 = now;
                                g.2 = "phone".into();
                                drop(g);
                                bt_apply_hw_volume(pct);
                            }
                        }
                        in_vol = false;
                    }
                    line.clear();
                }
            }
            let _ = child.wait();
        }
        thread::sleep(Duration::from_secs(2));
    });

    let s2 = std::sync::Arc::clone(&shared);
    thread::spawn(move || {
        let mut last_mt = fs::metadata(STATE_FILE).and_then(|m| m.modified()).ok();
        loop {
            if let Ok(mt) = fs::metadata(STATE_FILE).and_then(|m| m.modified()) {
                if Some(mt) != last_mt {
                    last_mt = Some(mt);
                    let pct = read_vol();
                    let update = {
                        let mut g = s2.lock().unwrap();
                        let now = now_unix_secs();
                        if (pct - g.0).abs() > 1 && !(g.2 == "phone" && now - g.1 < 0.6) {
                            g.0 = pct;
                            g.1 = now;
                            g.2 = "station".into();
                            true
                        } else {
                            false
                        }
                    };
                    if update {
                        let val127 = ((pct as f64) * 127.0 / 100.0).round() as u16;
                        let val16 = (val127 << 8) | val127;
                        if let Ok(mut conn) = DbusConn::connect() {
                            for tp in dbus_bluez_transports() {
                                conn.set_prop_u16(&tp, "org.bluez", "org.bluez.MediaTransport1", "Volume", val127);
                            }
                            for pp in dbus_bluealsa_pcms() {
                                conn.set_prop_u16(&pp, "org.bluealsa", "org.bluealsa.PCM1", "Volume", val16);
                            }
                        }
                    }
                }
            }
            thread::sleep(Duration::from_millis(50));
        }
    });

    let mut last_bt: Option<&'static str> = None;
    loop {
        let is_active = if dbus_bluealsa_pcms().is_empty() { "0" } else { "1" };
        if Some(is_active) != last_bt {
            let _ = atomic_write_bytes(Path::new("/var/run/atrios/bt_active"), is_active.as_bytes(), 0o644, false);
            last_bt = Some(is_active);
        }
        thread::sleep(Duration::from_secs(1));
    }
}

// =====================================================================
// 6. RTK-BT-BRIDGE & RTK-BT-INIT (Realtek H4 / H5 SLIP)
// =====================================================================
static BRIDGE_RUNNING: AtomicBool = AtomicBool::new(true);
extern "C" fn on_bridge_sig(_sig: c_int) {
    BRIDGE_RUNNING.store(false, Ordering::SeqCst);
}

fn crc16_x25(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &b in data {
        crc ^= b as u16;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0x8408 } else { crc >> 1 };
        }
    }
    crc
}

fn bitrev16(val: u16) -> u16 {
    val.reverse_bits()
}

fn slip_encode(data: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(data.len() + 8);
    out.push(0xC0);
    for &b in data {
        if b == 0xC0 {
            out.extend_from_slice(&[0xDB, 0xDC]);
        } else if b == 0xDB {
            out.extend_from_slice(&[0xDB, 0xDD]);
        } else {
            out.push(b);
        }
    }
    out.push(0xC0);
    out
}

fn slip_decode(raw: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(raw.len());
    let mut i = 0;
    while i < raw.len() {
        if raw[i] == 0xDB && i + 1 < raw.len() {
            match raw[i + 1] {
                0xDC => out.push(0xC0),
                0xDD => out.push(0xDB),
                x => out.push(x),
            }
            i += 2;
        } else {
            out.push(raw[i]);
            i += 1;
        }
    }
    out
}

fn h5_frame(ptype: u8, payload: &[u8], seq: u8, ack: u8, reliable: bool, use_crc: bool) -> Vec<u8> {
    let mut b0 = (seq & 7) | ((ack & 7) << 3) | if reliable { 0x80 } else { 0 };
    if use_crc && (ptype == 1 || ptype == 2) {
        b0 |= 0x40;
    }
    let plen = payload.len();
    let b1 = (ptype & 0x0F) | (((plen & 0x0F) as u8) << 4);
    let b2 = ((plen >> 4) & 0xFF) as u8;
    let b3 = !b0.wrapping_add(b1).wrapping_add(b2);
    let mut raw = Vec::with_capacity(6 + plen);
    raw.extend_from_slice(&[b0, b1, b2, b3]);
    raw.extend_from_slice(payload);
    if b0 & 0x40 != 0 {
        let crc = bitrev16(crc16_x25(&raw));
        raw.push((crc >> 8) as u8);
        raw.push((crc & 0xFF) as u8);
    }
    slip_encode(&raw)
}

fn config_uart(fd: c_int, baud: u32, parity_even: bool, rtscts: bool) -> bool {
    let mut t = unsafe { std::mem::zeroed::<Termios>() };
    if unsafe { tcgetattr(fd, &mut t) } != 0 {
        return false;
    }
    let mut cflag = CS8 | CREAD | CLOCAL | baud;
    if rtscts { cflag |= CRTSCTS; }
    if parity_even { cflag |= PARENB; cflag &= !PARODD; }
    t.c_iflag = 0;
    t.c_oflag = 0;
    t.c_lflag = 0;
    t.c_cflag = cflag;
    t.c_ispeed = baud;
    t.c_ospeed = baud;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    unsafe {
        if tcsetattr(fd, TCSANOW, &t) != 0 {
            return false;
        }
        tcflush(fd, TCIOFLUSH);
    }
    true
}

pub fn run_rtk_bt_bridge(_args: &[String]) -> i32 {
    unsafe {
        signal(SIGTERM, on_bridge_sig as *const () as usize);
        signal(SIGINT, on_bridge_sig as *const () as usize);
    }
    let _ = fs::write("/var/run/rtk-bt-bridge.pid", format!("{}\n", std::process::id()));
    let uart_dev = ["/dev/ttyAML1", "/dev/ttyAML2", "/dev/ttyS1"]
        .into_iter()
        .find(|p| Path::new(p).exists())
        .unwrap_or("/dev/ttyAML1");

    let hw_reset = || {
        for pin in [493, 494] {
            if !Path::new(&format!("/sys/class/gpio/gpio{pin}")).exists() {
                let _ = fs::write("/sys/class/gpio/export", pin.to_string());
                let _ = fs::write(format!("/sys/class/gpio/gpio{pin}/direction"), "out");
            }
        }
        if Path::new("/sys/class/gpio/gpio493/value").exists() {
            let _ = fs::write("/sys/class/gpio/gpio493/value", "0");
            let _ = fs::write("/sys/class/gpio/gpio494/value", "0");
            thread::sleep(Duration::from_millis(500));
            let _ = fs::write("/sys/class/gpio/gpio493/value", "1");
            thread::sleep(Duration::from_millis(200));
            let _ = fs::write("/sys/class/gpio/gpio494/value", "1");
            thread::sleep(Duration::from_millis(500));
        }
    };

    println!("[rtk-bt-bridge] Resetting RTL8822CS module on {uart_dev}...");
    hw_reset();
    let ok = Command::new("/usr/bin/rtk_hciattach")
        .args(["-s", "115200", uart_dev, "rtk_h5"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
    if !ok {
        hw_reset();
        let _ = Command::new("/usr/bin/rtk_hciattach")
            .args(["-s", "115200", uart_dev, "rtk_h5"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
    }

    let Ok(uart_f) = OpenOptions::new().read(true).write(true).custom_flags(0o4400).open(uart_dev) else {
        eprintln!("[rtk-bt-bridge] Cannot open {uart_dev}");
        return 1;
    };
    let uart_fd = uart_f.as_raw_fd();
    config_uart(uart_fd, B3000000, true, true);

    let master_fd = unsafe { posix_openpt(0o402) };
    if master_fd < 0 || unsafe { grantpt(master_fd) } != 0 || unsafe { unlockpt(master_fd) } != 0 {
        eprintln!("[rtk-bt-bridge] posix_openpt failed");
        return 1;
    }
    let mut name_buf = [0 as std::os::raw::c_char; 128];
    if unsafe { ptsname_r(master_fd, name_buf.as_mut_ptr(), name_buf.len()) } != 0 {
        return 1;
    }
    let slave_name = unsafe { std::ffi::CStr::from_ptr(name_buf.as_ptr()).to_string_lossy().into_owned() };
    let Ok(slave_f) = OpenOptions::new().read(true).write(true).custom_flags(0o400).open(&slave_name) else {
        return 1;
    };
    let slave_fd = slave_f.as_raw_fd();
    unsafe {
        let mut t = std::mem::zeroed::<Termios>();
        tcgetattr(master_fd, &mut t);
        cfmakeraw(&mut t);
        tcsetattr(master_fd, TCSANOW, &t);
        tcgetattr(slave_fd, &mut t);
        cfmakeraw(&mut t);
        tcsetattr(slave_fd, TCSANOW, &t);
        let ldisc: c_int = 15;
        ioctl(slave_fd, TIOCSETD, &ldisc as *const c_int);
        ioctl(slave_fd, HCIUARTSETPROTO, 0usize);
    }
    println!("[rtk-bt-bridge] Bridge operational: Kernel H4 <-> Realtek H5 UART.");

    let mut tx_seq = 0u8;
    let mut rx_seq = 0u8;
    let mut uart_rx: Vec<u8> = Vec::new();
    let mut master_rx: Vec<u8> = Vec::new();
    let mut tmp = [0u8; 2048];

    while BRIDGE_RUNNING.load(Ordering::SeqCst) {
        let mut pfds = [
            PollFd { fd: uart_fd, events: POLLIN, revents: 0 },
            PollFd { fd: master_fd, events: POLLIN, revents: 0 },
        ];
        unsafe { poll(pfds.as_mut_ptr(), 2, 500) };

        if pfds[0].revents & POLLIN != 0 {
            let n = recv_or_read(uart_fd, &mut tmp);
            if n <= 0 { break; }
            uart_rx.extend_from_slice(&tmp[..n as usize]);
            while let Some(idx) = uart_rx.iter().position(|&b| b == 0xC0) {
                let raw_pkt: Vec<u8> = uart_rx.drain(..=idx).collect();
                if idx == 0 { continue; }
                let dec = slip_decode(&raw_pkt[..idx]);
                if dec.len() < 4 || dec[0].wrapping_add(dec[1]).wrapping_add(dec[2]).wrapping_add(dec[3]) != 0xFF {
                    continue;
                }
                let plen = (((dec[1] >> 4) & 0x0F) as usize) | ((dec[2] as usize) << 4);
                let ptype = dec[1] & 0x0F;
                let has_crc = dec[0] & 0x40 != 0;
                if dec.len() < 4 + plen + if has_crc { 2 } else { 0 } {
                    continue;
                }
                if has_crc {
                    let calc = bitrev16(crc16_x25(&dec[..4 + plen]));
                    let got = ((dec[4 + plen] as u16) << 8) | (dec[4 + plen + 1] as u16);
                    if calc != got { continue; }
                }
                let payload = &dec[4..4 + plen];
                if dec[0] & 0x80 != 0 {
                    let pseq = dec[0] & 7;
                    if pseq != rx_seq {
                        write_fd(uart_fd, &h5_frame(0, &[], 0, rx_seq, false, false));
                        continue;
                    }
                    rx_seq = (rx_seq + 1) & 7;
                    write_fd(uart_fd, &h5_frame(0, &[], 0, rx_seq, false, false));
                }
                if ptype == 0x0F {
                    if payload.starts_with(&[0x05, 0xFA]) {
                        write_fd(uart_fd, &h5_frame(0x0F, &[0x06, 0xF9], 0, 0, false, false));
                    }
                    continue;
                }
                if matches!(ptype, 2 | 3 | 4) {
                    let mut out = Vec::with_capacity(1 + payload.len());
                    out.push(ptype);
                    out.extend_from_slice(payload);
                    write_fd(master_fd, &out);
                }
            }
        }

        if pfds[1].revents & POLLIN != 0 {
            let n = recv_or_read(master_fd, &mut tmp);
            if n <= 0 { break; }
            master_rx.extend_from_slice(&tmp[..n as usize]);
            while master_rx.len() >= 3 {
                let ptype = master_rx[0];
                let total = match ptype {
                    1 | 3 if master_rx.len() >= 4 => 4 + (master_rx[3] as usize),
                    2 if master_rx.len() >= 5 => 5 + (u16::from_le_bytes([master_rx[3], master_rx[4]]) as usize),
                    1..=3 => break,
                    _ => {
                        master_rx.remove(0);
                        continue;
                    }
                };
                if master_rx.len() < total { break; }
                let payload: Vec<u8> = master_rx.drain(..total).skip(1).collect();
                let pkt = h5_frame(ptype, &payload, tx_seq, rx_seq, true, true);
                tx_seq = (tx_seq + 1) & 7;
                write_fd(uart_fd, &pkt);
            }
        }
    }
    unsafe { close(master_fd); }
    let _ = fs::remove_file("/var/run/rtk-bt-bridge.pid");
    0
}

pub fn run_rtk_bt_init(args: &[String]) -> i32 {
    const STATE: &str = "/tmp/atrios_bt_state";
    const FW: &str = "/lib/firmware/rtlbt/rtl8822cs_fw";
    const CFG: &str = "/lib/firmware/rtlbt/rtl8822cs_config";
    let uart = ["/dev/ttyS1", "/dev/ttyAML1"].into_iter().find(|p| Path::new(p).exists()).unwrap_or("/dev/ttyS1");

    let reset_rfkill = || {
        if let Ok(entries) = fs::read_dir("/sys/class/rfkill") {
            for e in entries.flatten() {
                if fs::read_to_string(e.path().join("type")).map(|s| s.trim() == "bluetooth").unwrap_or(false) {
                    let st = e.path().join("state");
                    let _ = fs::write(&st, "0\n");
                    thread::sleep(Duration::from_millis(150));
                    let _ = fs::write(&st, "1\n");
                    thread::sleep(Duration::from_millis(500));
                }
            }
        }
    };

    let act = args.first().map(String::as_str).unwrap_or("init");
    if act == "scan" {
        if Command::new("hcitool").args(["-i", "hci0", "scan"]).status().map(|s| s.success()).unwrap_or(false) {
            return 0;
        }
    }
    reset_rfkill();
    let Ok(f) = OpenOptions::new().read(true).write(true).custom_flags(0o4400).open(uart) else {
        eprintln!("[BT] Error: {uart} not found");
        return 1;
    };
    let fd = f.as_raw_fd();
    for rtscts in [true, false] {
        config_uart(fd, B115200, false, rtscts);
        let pkt = [0x01u8, 0x01, 0x10, 0x00];
        write_fd(fd, &pkt);
        let mut pfd = PollFd { fd, events: POLLIN, revents: 0 };
        if unsafe { poll(&mut pfd, 1, 350) } > 0 {
            let mut resp = [0u8; 64];
            let n = recv_or_read(fd, &mut resp);
            if n > 0 && resp[0] == 0x04 {
                println!("[BT] Controller responded on {uart} (H4)");
                let _ = fs::write(STATE, "h4 Unknown\n");
                return 0;
            }
        }
    }
    let _ = (FW, CFG);
    println!("[BT] Checked UART controller on {uart}");
    0
}

// =====================================================================
// 7. WIFI-SCAN (nl80211 netlink / iw)
// =====================================================================
pub fn run_wifi_scan(_args: &[String]) -> i32 {
    if Command::new("iw")
        .args(["dev", "wlan0", "scan"])
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
    {
        return 0;
    }
    let Ok(c_if) = CString::new("wlan0") else { return 1 };
    let ifidx = unsafe { if_nametoindex(c_if.as_ptr()) };
    if ifidx == 0 {
        eprintln!("wlan0 not found");
        return 1;
    }
    let sock = unsafe { socket(AF_NETLINK, 3, NETLINK_GENERIC) };
    if sock < 0 {
        return 1;
    }
    let addr = SockAddrNl { nl_family: AF_NETLINK as u16, nl_pad: 0, nl_pid: 0, nl_groups: 0 };
    unsafe {
        if crate::sys::bind(sock, &addr as *const SockAddrNl as *const c_void, std::mem::size_of::<SockAddrNl>() as u32) != 0 {
            close(sock);
            return 1;
        }
    }
    let nla = |ty: u16, data: &[u8]| -> Vec<u8> {
        let len = (4 + data.len()) as u16;
        let pad = (4 - (len % 4)) % 4;
        let mut out = Vec::with_capacity(len as usize + pad as usize);
        out.extend_from_slice(&len.to_le_bytes());
        out.extend_from_slice(&ty.to_le_bytes());
        out.extend_from_slice(data);
        out.extend(std::iter::repeat(0u8).take(pad as usize));
        out
    };
    let mut req = vec![3u8, 1, 0, 0];
    req.extend_from_slice(&nla(2, b"nl80211\0"));
    let mut pkt = Vec::with_capacity(16 + req.len());
    pkt.extend_from_slice(&((16 + req.len()) as u32).to_le_bytes());
    pkt.extend_from_slice(&0x10u16.to_le_bytes());
    pkt.extend_from_slice(&1u16.to_le_bytes());
    pkt.extend_from_slice(&1u32.to_le_bytes());
    pkt.extend_from_slice(&0u32.to_le_bytes());
    pkt.extend_from_slice(&req);
    unsafe {
        send(sock, pkt.as_ptr() as *const c_void, pkt.len(), 0);
        let mut resp = [0u8; 4096];
        let _ = recv(sock, resp.as_mut_ptr() as *mut c_void, resp.len(), 0);
        let _ = setsockopt(sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &0u32 as *const u32 as *const c_void, 4);
        close(sock);
    }
    let _ = ifidx;
    0
}
