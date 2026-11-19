use std::collections::BTreeMap;
use std::fs::{self, OpenOptions};
use std::os::unix::io::AsRawFd;
use std::path::Path;

use crate::json::Json;
use crate::sys::{atomic_write_bytes, flock, now_unix_secs, LOCK_EX};

pub const ERROR_FILE: &str = "/var/run/atrios/errors.json";

#[derive(Clone, Debug)]
pub struct ErrorEntry {
    pub source: String,
    pub kind: String,
    pub code: i64,
    pub message: String,
    pub time: f64,
    pub expires: f64,
}

pub fn error_code(kind: &str) -> Option<i64> {
    match kind {
        "network" => Some(1),
        "tls" => Some(2),
        "server" => Some(3),
        "audio" => Some(4),
        "update" => Some(5),
        "signature" => Some(6),
        "storage" => Some(7),
        _ => None,
    }
}

pub fn error_kind_from_msg(msg: &str, default: &str) -> &'static str {
    let low = msg.to_ascii_lowercase();
    if low.contains("tls") || low.contains("ssl") || low.contains("certificate") || low.contains("x509") {
        "tls"
    } else if low.contains("enospc") || low.contains("no space") || low.contains("read-only") || low.contains("eio") {
        "storage"
    } else if low.contains("timed out") || low.contains("timeout") || low.contains("connect") || low.contains("dns") || low.contains("resolve") {
        "network"
    } else if default == "server" {
        "server"
    } else {
        "update"
    }
}

pub fn get_errors_at(path: &Path) -> BTreeMap<String, ErrorEntry> {
    let mut out = BTreeMap::new();
    let Ok(txt) = fs::read_to_string(path) else {
        return out;
    };
    let Ok(Json::Obj(map)) = Json::parse(&txt) else {
        return out;
    };
    let now = now_unix_secs();
    for (k, v) in map {
        let Some(obj) = v.as_obj() else { continue };
        let Some(expires) = obj.get("expires").and_then(|x| x.as_f64()) else { continue };
        let Some(time) = obj.get("time").and_then(|x| x.as_f64()) else { continue };
        let Some(code) = obj.get("code").and_then(|x| x.as_i64()) else { continue };
        if expires <= now || !(1..=7).contains(&code) {
            continue;
        }
        let source = obj.get("source").and_then(|x| x.as_str()).unwrap_or(&k).to_string();
        let kind = obj.get("kind").and_then(|x| x.as_str()).unwrap_or("default").to_string();
        let message = obj.get("message").and_then(|x| x.as_str()).unwrap_or("").to_string();
        out.insert(k, ErrorEntry { source, kind, code, message, time, expires });
    }
    out
}

pub fn active_error() -> Option<ErrorEntry> {
    let errors = get_errors_at(Path::new(ERROR_FILE));
    errors
        .into_values()
        .max_by(|a, b| a.code.cmp(&b.code).then_with(|| a.time.total_cmp(&b.time)))
}

pub fn errors_json_value() -> Json {
    let mut map = BTreeMap::new();
    for (k, v) in get_errors_at(Path::new(ERROR_FILE)) {
        let mut entry = BTreeMap::new();
        entry.insert("source".into(), Json::Str(v.source));
        entry.insert("kind".into(), Json::Str(v.kind));
        entry.insert("code".into(), Json::Num(v.code as f64));
        entry.insert("message".into(), Json::Str(v.message));
        entry.insert("time".into(), Json::Num(v.time));
        entry.insert("expires".into(), Json::Num(v.expires));
        map.insert(k, Json::Obj(entry));
    }
    Json::Obj(map)
}

pub fn set_error(source: &str, kind: Option<&str>, message: &str, ttl: u64) {
    let path = Path::new(ERROR_FILE);
    let Some(parent) = path.parent() else { return };
    if fs::create_dir_all(parent).is_err() {
        return;
    }
    let lock_path = format!("{ERROR_FILE}.lock");
    let Ok(lock_file) = OpenOptions::new().create(true).append(true).open(&lock_path) else {
        return;
    };
    unsafe {
        if flock(lock_file.as_raw_fd(), LOCK_EX) != 0 {
            return;
        }
    }
    let mut current = get_errors_at(path);
    current.remove(source);
    if let Some(k) = kind.filter(|s| !s.is_empty()) {
        let now = now_unix_secs();
        let code = error_code(k).unwrap_or(5);
        let msg: String = message.chars().take(240).collect();
        let clamped_ttl = ttl.clamp(1, 86400) as f64;
        current.insert(
            source.to_string(),
            ErrorEntry {
                source: source.to_string(),
                kind: k.to_string(),
                code,
                message: msg,
                time: now,
                expires: now + clamped_ttl,
            },
        );
    }
    let mut root = BTreeMap::new();
    for (k, v) in current {
        let mut entry = BTreeMap::new();
        entry.insert("source".into(), Json::Str(v.source));
        entry.insert("kind".into(), Json::Str(v.kind));
        entry.insert("code".into(), Json::Num(v.code as f64));
        entry.insert("message".into(), Json::Str(v.message));
        entry.insert("time".into(), Json::Num(v.time));
        entry.insert("expires".into(), Json::Num(v.expires));
        root.insert(k, Json::Obj(entry));
    }
    let _ = atomic_write_bytes(path, Json::Obj(root).to_string_compact().as_bytes(), 0o644, false);
}
