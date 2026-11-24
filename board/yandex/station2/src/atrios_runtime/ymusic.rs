use std::fs;
use std::os::unix::net::UnixDatagram;
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{mpsc, Arc};
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

fn now_unix_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or_else(|_| now_unix_i64() * 1000)
}

static STATUS_SEQ: AtomicU64 = AtomicU64::new(0);

use crate::https::{request_bytes, request_json};
use crate::json::{obj, Json};
use crate::sys::{atomic_write_bytes, kill, now_unix_i64, signal, SIGINT, SIGTERM};

const SIGSTOP_NUM: i32 = 19;
const SIGCONT_NUM: i32 = 18;
const SIGKILL_NUM: i32 = 9;

pub const YMUSIC_CONFIG_FILE: &str = "/var/lib/atrios/ymusic.json";
pub const YMUSIC_STATUS_FILE: &str = "/var/run/atrios/ymusic_status.json";
pub const YMUSIC_CMD_SOCK: &str = "/var/run/atrios/ymusic.sock";

const API_BASE: &str = "https://api.music.yandex.net";
const CLIENT_HEADER: (&str, &str) = ("X-Yandex-Music-Client", "YandexMusicAndroid/24023621");
const LANG_HEADER: (&str, &str) = ("Accept-Language", "ru");
const SIGN_SALT: &str = "XGRlBW9FXlekgbPrRHuSiA";

static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

extern "C" fn on_stop(_sig: std::os::raw::c_int) {
    STOP_REQUESTED.store(true, Ordering::SeqCst);
}

// --- Pure Rust RFC 1321 MD5 (zero external crates) ---
const MD5_S: [u32; 64] = [
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
];

const MD5_K: [u32; 64] = [
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
];

pub(crate) fn md5_hex(input: &[u8]) -> String {
    let mut a0: u32 = 0x67452301;
    let mut b0: u32 = 0xefcdab89;
    let mut c0: u32 = 0x98badcfe;
    let mut d0: u32 = 0x10325476;

    let bit_len = (input.len() as u64).wrapping_mul(8);
    let mut msg = input.to_vec();
    msg.push(0x80);
    while (msg.len() % 64) != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bit_len.to_le_bytes());

    for chunk in msg.chunks_exact(64) {
        let mut m = [0u32; 16];
        for (i, w) in m.iter_mut().enumerate() {
            *w = u32::from_le_bytes(chunk[i * 4..i * 4 + 4].try_into().unwrap());
        }
        let (mut a, mut b, mut c, mut d) = (a0, b0, c0, d0);
        for i in 0..64 {
            let (f, g) = match i {
                0..=15 => ((b & c) | ((!b) & d), i),
                16..=31 => ((d & b) | ((!d) & c), (5 * i + 1) % 16),
                32..=47 => (b ^ c ^ d, (3 * i + 5) % 16),
                _ => (c ^ (b | (!d)), (7 * i) % 16),
            };
            let temp = d;
            d = c;
            c = b;
            let sum = a
                .wrapping_add(f)
                .wrapping_add(MD5_K[i])
                .wrapping_add(m[g]);
            b = b.wrapping_add(sum.rotate_left(MD5_S[i]));
            a = temp;
        }
        a0 = a0.wrapping_add(a);
        b0 = b0.wrapping_add(b);
        c0 = c0.wrapping_add(c);
        d0 = d0.wrapping_add(d);
    }

    let mut out = String::with_capacity(32);
    for w in [a0, b0, c0, d0] {
        for byte in w.to_le_bytes() {
            use std::fmt::Write;
            let _ = write!(&mut out, "{byte:02x}");
        }
    }
    out
}

pub(crate) fn ymusic_stream_sign(path: &str, s: &str) -> String {
    let trimmed = path.strip_prefix('/').unwrap_or(path);
    md5_hex(format!("{SIGN_SALT}{trimmed}{s}").as_bytes())
}

pub(crate) fn build_stream_url(host: &str, codec: &str, path: &str, ts: &str, s: &str) -> String {
    let sign = ymusic_stream_sign(path, s);
    let trimmed = path.strip_prefix('/').unwrap_or(path);
    let c = if codec.is_empty() { "mp3" } else { codec };
    format!("https://{host}/get-{c}/{sign}/{ts}/{trimmed}")
}

fn format_cover_url(uri: Option<&str>) -> String {
    let Some(u) = uri else { return String::new(); };
    let u = u.trim();
    if u.is_empty() {
        return String::new();
    }
    let replaced = u.replace("%%", "200x200");
    if replaced.starts_with("http://") || replaced.starts_with("https://") {
        replaced
    } else {
        format!("https://{}", replaced.trim_start_matches('/'))
    }
}

fn format_avatar_url(avatar_id: Option<&str>) -> String {
    let Some(id) = avatar_id else { return String::new(); };
    let id = id.trim();
    if id.is_empty() {
        return String::new();
    }
    format!("https://avatars.yandex.net/get-yapic/{id}/islands-200")
}

fn url_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len() * 2);
    for &b in s.as_bytes() {
        if b.is_ascii_alphanumeric() || matches!(b, b'-' | b'_' | b'.' | b'~') {
            out.push(b as char);
        } else if b == b' ' {
            out.push_str("%20");
        } else {
            use std::fmt::Write;
            let _ = write!(&mut out, "%{b:02X}");
        }
    }
    out
}

fn xml_tag(xml: &str, tag: &str) -> Option<String> {
    let open = format!("<{tag}>");
    let close = format!("</{tag}>");
    let start = xml.find(&open)? + open.len();
    let end = xml[start..].find(&close)? + start;
    Some(xml[start..end].trim().to_string())
}

pub(crate) fn parse_download_info_payload(raw: &[u8]) -> Result<(String, String, String, String), String> {
    let txt = String::from_utf8_lossy(raw);
    let trimmed = txt.trim();
    if trimmed.starts_with('{') {
        let j = Json::parse(trimmed)?;
        let host = j.get_str("host").ok_or("Missing host in download info")?.to_string();
        let path = j.get_str("path").ok_or("Missing path in download info")?.to_string();
        let ts = j.get_str("ts").ok_or("Missing ts in download info")?.to_string();
        let s = j.get_str("s").ok_or("Missing s in download info")?.to_string();
        return Ok((host, path, ts, s));
    }
    let host = xml_tag(trimmed, "host").ok_or("Missing <host> in download info XML")?;
    let path = xml_tag(trimmed, "path").ok_or("Missing <path> in download info XML")?;
    let ts = xml_tag(trimmed, "ts").ok_or("Missing <ts> in download info XML")?;
    let s = xml_tag(trimmed, "s").ok_or("Missing <s> in download info XML")?;
    Ok((host, path, ts, s))
}

// --- Auth & Status Structures ---
#[derive(Clone, Debug, Default)]
pub struct YMusicAuth {
    pub token: String,
    pub uid: i64,
    pub login: String,
    pub display_name: String,
    pub avatar_url: String,
}

#[derive(Clone, Debug)]
pub struct TrackInfo {
    pub id: String,
    pub title: String,
    pub artist: String,
    pub album: String,
    pub cover_url: String,
    pub duration_ms: i64,
    pub liked: bool,
}

#[derive(Clone, Debug)]
pub struct AlbumInfo {
    pub id: String,
    pub title: String,
    pub artist: String,
    pub year: i64,
    pub track_count: i64,
    pub cover_url: String,
}

#[derive(Clone, Debug)]
pub struct YMusicStatus {
    pub logged_in: bool,
    pub login: String,
    pub display_name: String,
    pub avatar_url: String,
    pub uid: i64,
    pub state: String,
    pub mode: String,
    pub track_id: String,
    pub title: String,
    pub artist: String,
    pub album: String,
    pub cover_url: String,
    pub next_title: String,
    pub next_artist: String,
    pub next_cover_url: String,
    pub prev_title: String,
    pub prev_artist: String,
    pub prev_cover_url: String,
    pub duration_ms: i64,
    pub position_ms: i64,
    pub started_ts: i64,
    pub liked: bool,
    pub repeat: bool,
    pub can_prev: bool,
    pub can_next: bool,
    pub error: Option<String>,
    pub seq: u64,
    pub updated_ms: i64,
    pub updated_ts: i64,
}

impl YMusicStatus {
    pub fn default_idle() -> Self {
        let auth = load_auth();
        let now_ms = now_unix_ms();
        Self {
            logged_in: auth.is_some(),
            login: auth.as_ref().map(|a| a.login.clone()).unwrap_or_default(),
            display_name: auth.as_ref().map(|a| a.display_name.clone()).unwrap_or_default(),
            avatar_url: auth.as_ref().map(|a| a.avatar_url.clone()).unwrap_or_default(),
            uid: auth.as_ref().map(|a| a.uid).unwrap_or(0),
            state: "idle".into(),
            mode: "none".into(),
            track_id: String::new(),
            title: String::new(),
            artist: String::new(),
            album: String::new(),
            cover_url: String::new(),
            next_title: String::new(),
            next_artist: String::new(),
            next_cover_url: String::new(),
            prev_title: String::new(),
            prev_artist: String::new(),
            prev_cover_url: String::new(),
            duration_ms: 0,
            position_ms: 0,
            started_ts: 0,
            liked: false,
            repeat: false,
            can_prev: false,
            can_next: true,
            error: None,
            seq: 0,
            updated_ms: now_ms,
            updated_ts: now_ms / 1000,
        }
    }

    pub fn from_json(j: &Json) -> Self {
        let mut st = Self::default_idle();
        if let Some(s) = j.get_str("state") {
            if !s.is_empty() {
                st.state = s.into();
            }
        }
        if let Some(s) = j.get_str("mode") {
            if !s.is_empty() {
                st.mode = s.into();
            }
        }
        if let Some(s) = j.get_str("track_id") {
            st.track_id = s.into();
        }
        if let Some(s) = j.get_str("title") {
            st.title = s.into();
        }
        if let Some(s) = j.get_str("artist") {
            st.artist = s.into();
        }
        if let Some(s) = j.get_str("album") {
            st.album = s.into();
        }
        if let Some(s) = j.get_str("cover_url") {
            st.cover_url = s.into();
        }
        if let Some(s) = j.get_str("next_title") { st.next_title = s.into(); }
        if let Some(s) = j.get_str("next_artist") { st.next_artist = s.into(); }
        if let Some(s) = j.get_str("next_cover_url") { st.next_cover_url = s.into(); }
        if let Some(s) = j.get_str("prev_title") { st.prev_title = s.into(); }
        if let Some(s) = j.get_str("prev_artist") { st.prev_artist = s.into(); }
        if let Some(s) = j.get_str("prev_cover_url") { st.prev_cover_url = s.into(); }
        if let Some(n) = j.get("duration_ms").and_then(|v| v.as_i64()) {
            st.duration_ms = n.max(0);
        }
        if let Some(n) = j.get("position_ms").and_then(|v| v.as_i64()) {
            st.position_ms = n.max(0);
        }
        if let Some(n) = j.get("started_ts").and_then(|v| v.as_i64()) {
            st.started_ts = n.max(0);
        }
        if let Some(b) = j.get("liked").and_then(|v| v.as_bool()) {
            st.liked = b;
        }
        if let Some(b) = j.get("repeat").and_then(|v| v.as_bool()) {
            st.repeat = b;
        }
        if let Some(b) = j.get("can_prev").and_then(|v| v.as_bool()) {
            st.can_prev = b;
        }
        if let Some(b) = j.get("can_next").and_then(|v| v.as_bool()) {
            st.can_next = b;
        }
        if let Some(s) = j.get_str("error") {
            if !s.is_empty() {
                st.error = Some(s.into());
            }
        }
        if let Some(n) = j.get("seq").and_then(|v| v.as_i64()) {
            st.seq = n.max(0) as u64;
        }
        if let Some(n) = j.get("updated_ms").and_then(|v| v.as_i64()) {
            st.updated_ms = n;
        }
        if let Some(n) = j.get("updated_ts").and_then(|v| v.as_i64()) {
            st.updated_ts = n;
            if st.updated_ms == 0 {
                st.updated_ms = n * 1000;
            }
        }
        // Always refresh auth fields from disk so login/logout is authoritative
        if let Some(auth) = load_auth() {
            st.logged_in = true;
            st.login = auth.login;
            st.display_name = auth.display_name;
            st.avatar_url = auth.avatar_url;
            st.uid = auth.uid;
        } else {
            st.logged_in = false;
            st.login.clear();
            st.display_name.clear();
            st.avatar_url.clear();
            st.uid = 0;
        }
        st
    }

    pub fn to_json(&self) -> Json {
        obj(vec![
            ("logged_in", Json::Bool(self.logged_in)),
            ("login", Json::Str(self.login.clone())),
            ("display_name", Json::Str(self.display_name.clone())),
            ("avatar_url", Json::Str(self.avatar_url.clone())),
            ("uid", Json::Num(self.uid as f64)),
            ("state", Json::Str(self.state.clone())),
            ("mode", Json::Str(self.mode.clone())),
            ("track_id", Json::Str(self.track_id.clone())),
            ("title", Json::Str(self.title.clone())),
            ("artist", Json::Str(self.artist.clone())),
            ("album", Json::Str(self.album.clone())),
            ("cover_url", Json::Str(self.cover_url.clone())),
            ("next_title", Json::Str(self.next_title.clone())),
            ("next_artist", Json::Str(self.next_artist.clone())),
            ("next_cover_url", Json::Str(self.next_cover_url.clone())),
            ("prev_title", Json::Str(self.prev_title.clone())),
            ("prev_artist", Json::Str(self.prev_artist.clone())),
            ("prev_cover_url", Json::Str(self.prev_cover_url.clone())),
            ("duration_ms", Json::Num(self.duration_ms as f64)),
            ("position_ms", Json::Num(self.position_ms as f64)),
            ("started_ts", Json::Num(self.started_ts as f64)),
            ("liked", Json::Bool(self.liked)),
            ("repeat", Json::Bool(self.repeat)),
            ("can_prev", Json::Bool(self.can_prev)),
            ("can_next", Json::Bool(self.can_next)),
            (
                "error",
                self.error.as_ref().map(|s| Json::Str(s.clone())).unwrap_or(Json::Null),
            ),
            ("audio_bass", Json::Num(if self.state == "playing" { read_audio_envelope().0 } else { 0.0 })),
            ("audio_mid", Json::Num(if self.state == "playing" { read_audio_envelope().1 } else { 0.0 })),
            ("audio_high", Json::Num(if self.state == "playing" { read_audio_envelope().2 } else { 0.0 })),
            ("audio_energy", Json::Num(if self.state == "playing" { read_audio_envelope().3 } else { 0.0 })),
            ("audio_beat", Json::Num(if self.state == "playing" { read_audio_envelope().4 } else { 0.0 })),
            ("seq", Json::Num(self.seq as f64)),
            ("updated_ms", Json::Num(self.updated_ms as f64)),
            ("updated_ts", Json::Num(self.updated_ts as f64)),
        ])
    }
}

pub fn load_auth_at(path: &Path) -> Option<YMusicAuth> {
    let txt = fs::read_to_string(path).ok()?;
    let j = Json::parse(&txt).ok()?;
    let token = j.get_str("token")?.trim().to_string();
    if token.is_empty() {
        return None;
    }
    Some(YMusicAuth {
        token,
        uid: j.get("uid").and_then(|v| v.as_i64()).unwrap_or(0),
        login: j.get_str("login").unwrap_or("").to_string(),
        display_name: j.get_str("display_name").unwrap_or("").to_string(),
        avatar_url: j.get_str("avatar_url").unwrap_or("").to_string(),
    })
}

pub fn load_auth() -> Option<YMusicAuth> {
    load_auth_at(Path::new(YMUSIC_CONFIG_FILE))
}

pub fn save_auth_at(path: &Path, auth: &YMusicAuth) -> Result<(), String> {
    if let Some(p) = path.parent() {
        fs::create_dir_all(p).map_err(|e| e.to_string())?;
    }
    let j = obj(vec![
        ("token", Json::Str(auth.token.clone())),
        ("uid", Json::Num(auth.uid as f64)),
        ("login", Json::Str(auth.login.clone())),
        ("display_name", Json::Str(auth.display_name.clone())),
        ("avatar_url", Json::Str(auth.avatar_url.clone())),
    ]);
    let body = format!("{}\n", j.to_string_pretty());
    atomic_write_bytes(path, body.as_bytes(), 0o600, true).map_err(|e| e.to_string())
}

pub fn read_status() -> YMusicStatus {
    if let Ok(txt) = fs::read_to_string(YMUSIC_STATUS_FILE) {
        if let Ok(j) = Json::parse(&txt) {
            return YMusicStatus::from_json(&j);
        }
    }
    YMusicStatus::default_idle()
}

pub fn read_status_json() -> Json {
    let mut st = read_status();
    if st.state == "playing" && st.duration_ms > 0 && st.started_ts > 0 {
        let now_ms = now_unix_ms();
        let start_ms = if st.started_ts > 10_000_000_000 { st.started_ts } else { st.started_ts * 1000 };
        let elapsed = (now_ms - start_ms).max(0);
        st.position_ms = (st.position_ms + elapsed).clamp(0, st.duration_ms);
    }
    st.to_json()
}

fn write_status(st: &mut YMusicStatus) {
    let now_ms = now_unix_ms();
    let prev = STATUS_SEQ.load(Ordering::SeqCst).max(st.seq);
    let next_seq = prev.saturating_add(1).max(now_ms.max(0) as u64);
    STATUS_SEQ.store(next_seq, Ordering::SeqCst);
    st.seq = next_seq;
    st.updated_ms = now_ms;
    st.updated_ts = now_ms / 1000;
    if let Some(p) = Path::new(YMUSIC_STATUS_FILE).parent() {
        let _ = fs::create_dir_all(p);
    }
    let body = format!("{}\n", st.to_json().to_string_pretty());
    let _ = atomic_write_bytes(Path::new(YMUSIC_STATUS_FILE), body.as_bytes(), 0o644, false);
}

pub fn is_active() -> bool {
    if !Path::new(YMUSIC_CMD_SOCK).exists() {
        return false;
    }
    let st = read_status();
    matches!(st.state.as_str(), "playing" | "paused" | "loading")
}

pub fn now_playing() -> Option<(String, String)> {
    let st = read_status();
    if matches!(st.state.as_str(), "playing" | "paused") && !st.title.is_empty() {
        Some((st.title, st.artist))
    } else {
        None
    }
}

pub fn announce_now_playing() {
    let msg = match now_playing() {
        Some((title, artist)) if !artist.is_empty() => format!("Сейчас играет {artist} — {title}"),
        Some((title, _)) => format!("Сейчас играет {title}"),
        None => "Сейчас музыка не играет".to_string(),
    };
    println!("[ymusic] {msg}");
    if !crate::atriai::speak_via_remote_tts(&msg, "neutral", false) {
        let _ = Command::new("/usr/bin/atrios")
            .args(["screen", "ticker", &msg])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn();
    }
}

// --- Yandex Music REST API ---
fn api_get_json(token: &str, endpoint: &str) -> Result<Json, String> {
    let url = format!("{API_BASE}{endpoint}");
    let auth_hdr = format!("OAuth {token}");
    let hdrs = [("Authorization", auth_hdr.as_str()), CLIENT_HEADER, LANG_HEADER];
    request_json(&url, None, &hdrs, None, 15, 2 * 1024 * 1024, None)
}

fn api_post_form_json(token: &str, endpoint: &str, form_body: &str) -> Result<Json, String> {
    let url = format!("{API_BASE}{endpoint}");
    let auth_hdr = format!("OAuth {token}");
    let hdrs = [
        ("Authorization", auth_hdr.as_str()),
        CLIENT_HEADER,
        ("Content-Type", "application/x-www-form-urlencoded"),
        ("Accept", "application/json"),
    ];
    let bytes = request_bytes(&url, Some(form_body.as_bytes()), &hdrs, None, 15, 2 * 1024 * 1024, None)?;
    Json::parse(&String::from_utf8_lossy(&bytes))
}

fn api_post_json(token: &str, endpoint: &str, payload: &Json) -> Result<Json, String> {
    let url = format!("{API_BASE}{endpoint}");
    let auth_hdr = format!("OAuth {token}");
    let hdrs = [("Authorization", auth_hdr.as_str()), CLIENT_HEADER, LANG_HEADER];
    request_json(&url, Some(payload), &hdrs, None, 15, 512 * 1024, None)
}

pub fn login_with_token(raw_token: &str) -> Result<YMusicStatus, String> {
    let token = raw_token.trim().strip_prefix("OAuth ").unwrap_or(raw_token.trim()).trim();
    if token.len() < 10 {
        return Err("Invalid OAuth token".into());
    }
    let resp = api_get_json(token, "/account/status")?;
    let acc = resp
        .get("result")
        .and_then(|r| r.get("account"))
        .ok_or("Invalid Yandex Music token or account response")?;
    let uid = acc.get("uid").and_then(|v| v.as_i64()).unwrap_or(0);
    if uid <= 0 {
        return Err("Yandex Music token is not authorized".into());
    }
    let login = acc.get_str("login").unwrap_or("").to_string();
    let display_name = acc
        .get_str("displayName")
        .or_else(|| acc.get_str("fullName"))
        .unwrap_or(&login)
        .to_string();
    let avatar_url = format_avatar_url(
        resp.get("result")
            .and_then(|r| r.get("defaultAvatarId").or_else(|| acc.get("defaultAvatarId")))
            .and_then(|v| v.as_str())
    );
    let auth = YMusicAuth {
        token: token.to_string(),
        uid,
        login: login.clone(),
        display_name: display_name.clone(),
        avatar_url: avatar_url.clone(),
    };
    save_auth_at(Path::new(YMUSIC_CONFIG_FILE), &auth)?;
    let mut st = read_status();
    st.logged_in = true;
    st.uid = uid;
    st.login = login;
    st.display_name = display_name;
    st.avatar_url = avatar_url;
    st.error = None;
    write_status(&mut st);
    Ok(st)
}

pub fn logout() -> YMusicStatus {
    let _ = send_cmd_only("stop");
    let _ = fs::remove_file(YMUSIC_CONFIG_FILE);
    let mut st = YMusicStatus::default_idle();
    st.logged_in = false;
    st.state = "idle".into();
    st.mode = "none".into();
    write_status(&mut st);
    st
}

fn parse_track_json(t: &Json, liked_default: bool) -> Option<TrackInfo> {
    let id = match t.get("id") {
        Some(Json::Str(s)) if !s.is_empty() => s.clone(),
        Some(Json::Num(n)) => format!("{}", *n as i64),
        _ => return None,
    };
    let title = t.get_str("title").unwrap_or("Unknown Track").to_string();
    let artist = t
        .get("artists")
        .and_then(|a| a.as_arr())
        .map(|arr| {
            arr.iter()
                .filter_map(|x| x.get_str("name"))
                .collect::<Vec<_>>()
                .join(", ")
        })
        .unwrap_or_default();
    let first_album = t.get("albums").and_then(|a| a.as_arr()).and_then(|arr| arr.first());
    let album = first_album.and_then(|al| al.get_str("title")).unwrap_or("").to_string();
    let cover_url = format_cover_url(
        t.get_str("coverUri").or_else(|| first_album.and_then(|al| al.get_str("coverUri")))
    );
    let duration_ms = t.get("durationMs").and_then(|v| v.as_i64()).unwrap_or(0);
    Some(TrackInfo { id, title, artist, album, cover_url, duration_ms, liked: liked_default })
}

/// MarshalX rotor_station_tracks: works for user:onyourwave, genre:*, mood:*, activity:*
fn station_search_fallback_query(st: &str) -> &'static str {
    let s = st.to_lowercase();
    if s.contains("party") || s.contains("вечерин") {
        "танцевальная вечеринка хиты"
    } else if s.contains("workout") || s.contains("run") {
        "музыка для бега и тренировок"
    } else if s.contains("calm") || s.contains("chill") {
        "спокойная музыка чиллаут"
    } else if s.contains("happy") || s.contains("радост") {
        "весёлая радостная музыка"
    } else if s.contains("love") || s.contains("романт") {
        "романтическая музыка для влюбленных"
    } else if s.contains("energetic") || s.contains("бодр") {
        "бодрая энергичная музыка"
    } else if s.contains("sad") {
        "грустная лирическая музыка"
    } else if s.contains("electronic") {
        "электронная музыка"
    } else {
        "популярные хиты"
    }
}

fn fetch_rotor_station(
    auth: &YMusicAuth,
    station: &str,
    queue_track_id: Option<&str>,
) -> Result<(String, Vec<TrackInfo>), String> {
    let raw_st = station.trim();
    let st = match raw_st {
        "" => "user:onyourwave",
        "mood:party" => "activity:party",
        "activity:workout" => "activity:run",
        other => other,
    };
    let ep = match queue_track_id {
        Some(tid) if !tid.is_empty() => {
            format!("/rotor/station/{st}/tracks?settings2=true&queue={}", url_encode(tid))
        }
        _ => format!("/rotor/station/{st}/tracks?settings2=true"),
    };
    if let Ok(resp) = api_get_json(&auth.token, &ep) {
        if let Some(res) = resp.get("result") {
            let batch_id = res.get_str("batchId").unwrap_or("").to_string();
            if let Some(seq) = res.get("sequence").and_then(|v| v.as_arr()) {
                let mut tracks = Vec::new();
                for item in seq {
                    let liked = item.get("liked").and_then(|v| v.as_bool()).unwrap_or(false);
                    if let Some(tr) = item.get("track").and_then(|t| parse_track_json(t, liked)) {
                        tracks.push(tr);
                    }
                }
                if !tracks.is_empty() {
                    return Ok((batch_id, tracks));
                }
            }
        }
    }
    // Seamless fallback to catalog search if rotor station tag is unavailable on Yandex
    let fb_q = station_search_fallback_query(st);
    let tracks = fetch_search(auth, fb_q)?;
    Ok((String::new(), tracks))
}

fn fetch_my_wave(auth: &YMusicAuth, queue_track_id: Option<&str>) -> Result<(String, Vec<TrackInfo>), String> {
    fetch_rotor_station(auth, "user:onyourwave", queue_track_id)
}

fn send_rotor_feedback_station(
    auth: &YMusicAuth,
    station: &str,
    batch_id: &str,
    kind: &str,
    track_id: Option<&str>,
    total_played_sec: Option<f64>,
) {
    let st = if station.trim().is_empty() { "user:onyourwave" } else { station.trim() };
    let mut fields = vec![
        ("type", Json::Str(kind.into())),
        ("timestamp", Json::Str(crate::sys::format_utc("%Y-%m-%dT%H:%M:%SZ", now_unix_i64()))),
    ];
    if let Some(tid) = track_id {
        fields.push(("trackId", Json::Str(tid.into())));
    }
    if let Some(secs) = total_played_sec {
        if secs.is_finite() && secs >= 0.0 {
            fields.push(("totalPlayedSeconds", Json::Num((secs * 10.0).round() / 10.0)));
        }
    }
    if kind == "radioStarted" {
        fields.push(("from", Json::Str("AtriOS-speaker".into())));
    }
    let ep = if batch_id.is_empty() {
        format!("/rotor/station/{st}/feedback")
    } else {
        format!("/rotor/station/{st}/feedback?batch-id={}", url_encode(batch_id))
    };
    let _ = api_post_json(&auth.token, &ep, &obj(fields));
}

fn send_rotor_feedback(
    auth: &YMusicAuth,
    batch_id: &str,
    kind: &str,
    track_id: Option<&str>,
    total_played_sec: Option<f64>,
) {
    send_rotor_feedback_station(auth, "user:onyourwave", batch_id, kind, track_id, total_played_sec);
}

/// MarshalX chart(): GET /landing3/chart
fn fetch_chart(auth: &YMusicAuth) -> Result<Vec<TrackInfo>, String> {
    let resp = api_get_json(&auth.token, "/landing3/chart")?;
    let arr = resp
        .get("result")
        .and_then(|r| r.get("chart"))
        .and_then(|c| c.get("tracks"))
        .and_then(|v| v.as_arr())
        .ok_or("Empty Yandex Music chart")?;
    let tracks: Vec<TrackInfo> = arr
        .iter()
        .take(30)
        .filter_map(|item| item.get("track").and_then(|t| parse_track_json(t, false)))
        .collect();
    if tracks.is_empty() {
        return Err("No playable tracks in chart".into());
    }
    Ok(tracks)
}

/// MarshalX users_playlists_list + users_playlists: supports direct "uid:kind" (including /feed generated playlists) or title search
fn fetch_user_playlist(auth: &YMusicAuth, query: &str) -> Result<Vec<TrackInfo>, String> {
    let raw = query.trim();
    let (owner_uid, kind) = if let Some((u_part, k_part)) = raw.split_once(':') {
        if let (Ok(u), Ok(k)) = (u_part.trim().parse::<i64>(), k_part.trim().parse::<i64>()) {
            (u, k)
        } else {
            return Err("Invalid playlist uid:kind".into());
        }
    } else {
        let q = raw.to_lowercase();
        let list_ep = format!("/users/{}/playlists/list", auth.uid);
        let resp = api_get_json(&auth.token, &list_ep)?;
        let arr = resp
            .get("result")
            .and_then(|v| v.as_arr())
            .ok_or("No playlists found")?;
        let chosen = arr
            .iter()
            .find(|pl| {
                let title = pl.get_str("title").unwrap_or("").to_lowercase();
                q.is_empty() || title.contains(&q)
            })
            .or_else(|| arr.first())
            .ok_or("User has no playlists")?;
        let k = chosen.get("kind").and_then(|v| v.as_i64()).ok_or("Playlist missing kind")?;
        (auth.uid, k)
    };
    let pl_ep = format!("/users/{owner_uid}/playlists/{kind}");
    let pl_resp = api_get_json(&auth.token, &pl_ep)?;
    let tr_arr = pl_resp
        .get("result")
        .and_then(|r| r.get("tracks"))
        .and_then(|v| v.as_arr())
        .ok_or("Playlist has no tracks")?;
    let tracks: Vec<TrackInfo> = tr_arr
        .iter()
        .take(100)
        .filter_map(|item| item.get("track").and_then(|t| parse_track_json(t, false)))
        .collect();
    if tracks.is_empty() {
        return Err("Playlist is empty".into());
    }
    Ok(tracks)
}

/// MarshalX queues_list + queue(id): continue playback from another Yandex Music device
fn fetch_remote_queue(auth: &YMusicAuth, queue_id: &str) -> Result<(Vec<TrackInfo>, usize), String> {
    let qid = if !queue_id.trim().is_empty() {
        queue_id.trim().to_string()
    } else {
        let list = api_get_json(&auth.token, "/queues")?;
        list.get("result")
            .and_then(|r| r.get("queues"))
            .and_then(|v| v.as_arr())
            .and_then(|arr| arr.first())
            .and_then(|q| q.get_str("id"))
            .map(|s| s.to_string())
            .ok_or("No active remote queues found")?
    };
    let q_resp = api_get_json(&auth.token, &format!("/queues/{}", url_encode(&qid)))?;
    let res = q_resp.get("result").ok_or("Empty queue response")?;
    let cur_idx = res.get("currentIndex").and_then(|v| v.as_i64()).unwrap_or(0).max(0) as usize;
    let raw_ids: Vec<String> = res
        .get("tracks")
        .and_then(|v| v.as_arr())
        .map(|arr| {
            arr.iter()
                .filter_map(|t| t.get_str("trackId").map(|s| s.to_string()))
                .collect()
        })
        .unwrap_or_default();
    if raw_ids.is_empty() {
        return Err("Remote queue is empty".into());
    }
    // Window around currentIndex so we stay fast & within 60 tracks
    let win_start = cur_idx.saturating_sub(5);
    let win_end = (win_start + 60).min(raw_ids.len());
    let slice = &raw_ids[win_start..win_end];
    let rel_idx = cur_idx.saturating_sub(win_start);
    let form = format!("track-ids={}", url_encode(&slice.join(",")));
    let det = api_post_form_json(&auth.token, "/tracks", &form)?;
    let tracks: Vec<TrackInfo> = det
        .get("result")
        .and_then(|v| v.as_arr())
        .map(|arr| arr.iter().filter_map(|t| parse_track_json(t, false)).collect())
        .unwrap_or_default();
    if tracks.is_empty() {
        return Err("No playable tracks in remote queue".into());
    }
    Ok((tracks, rel_idx.min(tracks.len().saturating_sub(1))))
}

fn fetch_likes(auth: &YMusicAuth) -> Result<Vec<TrackInfo>, String> {
    use std::sync::{Mutex, OnceLock};
    static LIKES_LIST_CACHE: OnceLock<Mutex<(i64, i64, Vec<TrackInfo>)>> = OnceLock::new();
    let cache = LIKES_LIST_CACHE.get_or_init(|| Mutex::new((0, 0, Vec::new())));
    let now = now_unix_i64();
    if let Ok(g) = cache.lock() {
        if g.0 == auth.uid && now - g.1 < 60 && !g.2.is_empty() {
            return Ok(g.2.clone());
        }
    }
    let ep = format!("/users/{}/likes/tracks", auth.uid);
    let resp = api_get_json(&auth.token, &ep)?;
    let arr = resp
        .get("result")
        .and_then(|r| r.get("library"))
        .and_then(|l| l.get("tracks"))
        .and_then(|t| t.as_arr())
        .ok_or("No liked tracks found")?;
    let mut ids: Vec<String> = Vec::new();
    for item in arr.iter().take(500) {
        if let Some(id) = item.get_str("id") {
            ids.push(id.to_string());
        } else if let Some(n) = item.get("id").and_then(|v| v.as_i64()) {
            ids.push(n.to_string());
        }
    }
    if ids.is_empty() {
        return Err("Your Liked Tracks list is empty".into());
    }
    let mut tracks: Vec<TrackInfo> = Vec::with_capacity(ids.len());
    for chunk in ids.chunks(100) {
        let body = format!("track-ids={}", url_encode(&chunk.join(",")));
        if let Ok(detail) = api_post_form_json(&auth.token, "/tracks", &body) {
            if let Some(list) = detail.get("result").and_then(|r| r.as_arr()) {
                tracks.extend(list.iter().filter_map(|t| parse_track_json(t, true)));
            }
        }
    }
    if tracks.is_empty() {
        return Err("Could not parse liked tracks".into());
    }
    if let Ok(mut g) = cache.lock() {
        *g = (auth.uid, now, tracks.clone());
    }
    Ok(tracks)
}

fn fetch_search(auth: &YMusicAuth, query: &str) -> Result<Vec<TrackInfo>, String> {
    let q = query.trim();
    if q.is_empty() {
        return Err("Search query is empty".into());
    }
    let ep = format!("/search?type=track&page=0&nocorrect=false&text={}", url_encode(q));
    let resp = api_get_json(&auth.token, &ep)?;
    let arr = resp
        .get("result")
        .and_then(|r| r.get("tracks"))
        .and_then(|t| t.get("results"))
        .and_then(|v| v.as_arr())
        .ok_or_else(|| format!("Nothing found for \"{q}\""))?;
    let tracks: Vec<TrackInfo> = arr.iter().take(20).filter_map(|t| parse_track_json(t, false)).collect();
    if tracks.is_empty() {
        return Err(format!("No tracks found for \"{q}\""));
    }
    Ok(tracks)
}

fn fetch_single_track(auth: &YMusicAuth, track_id: &str) -> Result<Vec<TrackInfo>, String> {
    let tid = track_id.trim();
    if tid.is_empty() {
        return Err("Empty track_id".into());
    }
    let body = format!("track-ids={}", url_encode(tid));
    let detail = api_post_form_json(&auth.token, "/tracks", &body)?;
    let arr = detail
        .get("result")
        .and_then(|v| v.as_arr())
        .ok_or("Track not found")?;
    let tracks: Vec<TrackInfo> = arr
        .iter()
        .filter_map(|t| parse_track_json(t, false))
        .collect();
    if tracks.is_empty() {
        return Err("Track not playable".into());
    }
    Ok(tracks)
}

fn parse_album_json(al: &Json) -> Option<AlbumInfo> {
    let id = match al.get("id") {
        Some(Json::Str(s)) if !s.is_empty() => s.clone(),
        Some(Json::Num(n)) => format!("{}", *n as i64),
        _ => return None,
    };
    let title = al.get_str("title").unwrap_or("Unknown Album").to_string();
    let artist = al
        .get("artists")
        .and_then(|a| a.as_arr())
        .map(|arr| arr.iter().filter_map(|x| x.get_str("name")).collect::<Vec<_>>().join(", "))
        .unwrap_or_default();
    let year = al.get("year").and_then(|v| v.as_i64()).unwrap_or(0);
    let track_count = al.get("trackCount").and_then(|v| v.as_i64()).unwrap_or(0);
    let cover_url = format_cover_url(al.get_str("coverUri"));
    Some(AlbumInfo { id, title, artist, year, track_count, cover_url })
}

fn fetch_album_tracks(auth: &YMusicAuth, album_id: &str) -> Result<Vec<TrackInfo>, String> {
    let aid = album_id.trim();
    if aid.is_empty() {
        return Err("Empty album_id".into());
    }
    let ep = format!("/albums/{}/with-tracks", url_encode(aid));
    let resp = api_get_json(&auth.token, &ep)?;
    let res = resp.get("result").ok_or("Album not found")?;
    let album_cover = format_cover_url(res.get_str("coverUri"));
    let album_title = res.get_str("title").unwrap_or("").to_string();
    let mut tracks = Vec::new();
    if let Some(vols) = res.get("volumes").and_then(|v| v.as_arr()) {
        for vol in vols {
            if let Some(arr) = vol.as_arr() {
                for item in arr {
                    if let Some(mut tr) = parse_track_json(item, false) {
                        if tr.cover_url.is_empty() {
                            tr.cover_url = album_cover.clone();
                        }
                        if tr.album.is_empty() {
                            tr.album = album_title.clone();
                        }
                        tracks.push(tr);
                    }
                }
            }
        }
    }
    if tracks.is_empty() {
        return Err("Album has no playable tracks".into());
    }
    Ok(tracks)
}

pub fn search_catalog_json(query: &str) -> Result<(Vec<Json>, Vec<Json>), String> {
    let auth = load_auth().ok_or("Not logged in to Yandex Music")?;
    let q = query.trim();
    if q.is_empty() {
        return Ok((Vec::new(), Vec::new()));
    }
    let ep = format!("/search?type=all&page=0&nocorrect=false&text={}", url_encode(q));
    let resp = api_get_json(&auth.token, &ep)?;
    let res = resp.get("result").ok_or("Empty search result")?;

    let tracks: Vec<Json> = res
        .get("tracks")
        .and_then(|t| t.get("results"))
        .and_then(|v| v.as_arr())
        .map(|arr| {
            arr.iter()
                .take(12)
                .filter_map(|t| parse_track_json(t, false))
                .map(|t| {
                    obj(vec![
                        ("id", Json::Str(t.id)),
                        ("title", Json::Str(t.title)),
                        ("artist", Json::Str(t.artist)),
                        ("album", Json::Str(t.album)),
                        ("cover_url", Json::Str(t.cover_url)),
                        ("duration_ms", Json::Num(t.duration_ms as f64)),
                    ])
                })
                .collect()
        })
        .unwrap_or_default();

    let albums: Vec<Json> = res
        .get("albums")
        .and_then(|a| a.get("results"))
        .and_then(|v| v.as_arr())
        .map(|arr| {
            arr.iter()
                .take(8)
                .filter_map(parse_album_json)
                .map(|a| {
                    obj(vec![
                        ("id", Json::Str(a.id)),
                        ("title", Json::Str(a.title)),
                        ("artist", Json::Str(a.artist)),
                        ("year", Json::Num(a.year as f64)),
                        ("track_count", Json::Num(a.track_count as f64)),
                        ("cover_url", Json::Str(a.cover_url)),
                    ])
                })
                .collect()
        })
        .unwrap_or_default();

    Ok((tracks, albums))
}

pub fn search_tracks_json(query: &str) -> Result<Vec<Json>, String> {
    Ok(search_catalog_json(query)?.0)
}

pub fn fetch_collection_json() -> Result<Json, String> {
    use std::collections::{HashMap, HashSet};
    use std::sync::{Mutex, OnceLock};
    static COLLECTION_CACHE: OnceLock<Mutex<(i64, i64, Json)>> = OnceLock::new();
    let cache = COLLECTION_CACHE.get_or_init(|| Mutex::new((0, 0, Json::Null)));
    let auth = load_auth().ok_or("Not logged in to Yandex Music")?;
    let now = now_unix_i64();
    if let Ok(g) = cache.lock() {
        if g.0 == auth.uid && now - g.1 < 300 && !matches!(g.2, Json::Null) {
            return Ok(g.2.clone());
        }
    }

    // 1. Liked tracks (total count + top 30 metadata)
    let mut likes_count: i64 = 0;
    let mut liked_tracks_parsed: Vec<TrackInfo> = Vec::new();
    if let Ok(resp) = api_get_json(&auth.token, &format!("/users/{}/likes/tracks", auth.uid)) {
        if let Some(arr) = resp
            .get("result")
            .and_then(|r| r.get("library"))
            .and_then(|l| l.get("tracks"))
            .and_then(|v| v.as_arr())
        {
            likes_count = arr.len() as i64;
            let mut ids: Vec<String> = Vec::new();
            for item in arr.iter().take(300) {
                if let Some(id) = item.get_str("id") {
                    ids.push(id.to_string());
                } else if let Some(n) = item.get("id").and_then(|v| v.as_i64()) {
                    ids.push(n.to_string());
                }
            }
            for chunk in ids.chunks(100) {
                let body = format!("track-ids={}", url_encode(&chunk.join(",")));
                if let Ok(detail) = api_post_form_json(&auth.token, "/tracks", &body) {
                    if let Some(list) = detail.get("result").and_then(|r| r.as_arr()) {
                        liked_tracks_parsed.extend(list.iter().filter_map(|t| parse_track_json(t, true)));
                    }
                }
            }
        }
    }

    let liked_tracks_json: Vec<Json> = liked_tracks_parsed
        .iter()
        .map(|t| {
            obj(vec![
                ("id", Json::Str(t.id.clone())),
                ("title", Json::Str(t.title.clone())),
                ("artist", Json::Str(t.artist.clone())),
                ("album", Json::Str(t.album.clone())),
                ("cover_url", Json::Str(t.cover_url.clone())),
                ("duration_ms", Json::Num(t.duration_ms as f64)),
            ])
        })
        .collect();

    // 2. Favorite artists: combine /users/{uid}/likes/artists with top artists from liked tracks
    let mut favorite_artists: Vec<Json> = Vec::new();
    let mut seen_artists: HashSet<String> = HashSet::new();
    let mut liked_artist_counts: HashMap<String, (usize, String)> = HashMap::new();
    for tr in &liked_tracks_parsed {
        let main_a = tr.artist.split(',').next().unwrap_or("").trim();
        if !main_a.is_empty() {
            let e = liked_artist_counts
                .entry(main_a.to_string())
                .or_insert_with(|| (0, tr.cover_url.clone()));
            e.0 += 1;
            if e.1.is_empty() && !tr.cover_url.is_empty() {
                e.1 = tr.cover_url.clone();
            }
        }
    }
    if let Ok(resp) = api_get_json(&auth.token, &format!("/users/{}/likes/artists", auth.uid)) {
        if let Some(arr) = resp.get("result").and_then(|v| v.as_arr()) {
            for item in arr.iter().take(10) {
                let a = item.get("artist").unwrap_or(item);
                let name = a.get_str("name").unwrap_or("").trim().to_string();
                if name.is_empty() || !seen_artists.insert(name.to_lowercase()) {
                    continue;
                }
                let id = match a.get("id") {
                    Some(Json::Str(s)) => s.clone(),
                    Some(Json::Num(n)) => format!("{}", *n as i64),
                    _ => name.clone(),
                };
                let cov_uri = a
                    .get("cover")
                    .and_then(|c| c.get_str("uri"))
                    .or_else(|| a.get_str("ogImage"));
                let cov = format_cover_url(cov_uri);
                let cnt = liked_artist_counts.get(&name).map(|x| x.0).unwrap_or(0);
                favorite_artists.push(obj(vec![
                    ("id", Json::Str(id)),
                    ("name", Json::Str(name)),
                    ("cover_url", Json::Str(cov)),
                    ("track_count", Json::Num(cnt as f64)),
                ]));
            }
        }
    }
    if favorite_artists.len() < 8 {
        let mut ranked: Vec<(String, usize, String)> = liked_artist_counts
            .into_iter()
            .map(|(n, (c, cov))| (n, c, cov))
            .collect();
        ranked.sort_by(|a, b| b.1.cmp(&a.1));
        for (name, cnt, cov) in ranked {
            if favorite_artists.len() >= 10 {
                break;
            }
            if !seen_artists.insert(name.to_lowercase()) {
                continue;
            }
            favorite_artists.push(obj(vec![
                ("id", Json::Str(name.clone())),
                ("name", Json::Str(name)),
                ("cover_url", Json::Str(cov)),
                ("track_count", Json::Num(cnt as f64)),
            ]));
        }
    }

    // 3. User playlists (/users/{uid}/playlists/list)
    let mut playlists: Vec<Json> = Vec::new();
    if let Ok(resp) = api_get_json(&auth.token, &format!("/users/{}/playlists/list", auth.uid)) {
        if let Some(arr) = resp.get("result").and_then(|v| v.as_arr()) {
            for pl in arr.iter().take(12) {
                let title = pl.get_str("title").unwrap_or("").trim().to_string();
                if title.is_empty() {
                    continue;
                }
                let kind = pl.get("kind").and_then(|v| v.as_i64()).unwrap_or(0);
                let track_count = pl.get("trackCount").and_then(|v| v.as_i64()).unwrap_or(0);
                let cov_uri = pl
                    .get("cover")
                    .and_then(|c| {
                        c.get_str("uri").or_else(|| {
                            c.get("itemsUri")
                                .and_then(|u| u.as_arr())
                                .and_then(|a| a.first())
                                .and_then(|v| v.as_str())
                        })
                    })
                    .or_else(|| pl.get_str("ogImage"));
                playlists.push(obj(vec![
                    ("kind", Json::Num(kind as f64)),
                    ("title", Json::Str(title)),
                    ("track_count", Json::Num(track_count as f64)),
                    ("cover_url", Json::Str(format_cover_url(cov_uri))),
                ]));
            }
        }
    }

    // 4. Liked albums (/users/{uid}/likes/albums -> POST /albums)
    let mut liked_albums: Vec<Json> = Vec::new();
    if let Ok(resp) = api_get_json(&auth.token, &format!("/users/{}/likes/albums", auth.uid)) {
        if let Some(arr) = resp.get("result").and_then(|v| v.as_arr()) {
            let mut album_ids: Vec<String> = Vec::new();
            for item in arr.iter().take(12) {
                let id_v = item.get("id").or_else(|| item.get("album").and_then(|a| a.get("id")));
                match id_v {
                    Some(Json::Str(s)) if !s.is_empty() => album_ids.push(s.clone()),
                    Some(Json::Num(n)) => album_ids.push(format!("{}", *n as i64)),
                    _ => {}
                }
            }
            if !album_ids.is_empty() {
                let body = format!("album-ids={}", url_encode(&album_ids.join(",")));
                if let Ok(detail) = api_post_form_json(&auth.token, "/albums", &body) {
                    if let Some(list) = detail.get("result").and_then(|v| v.as_arr()) {
                        for al in list.iter().filter_map(parse_album_json) {
                            liked_albums.push(obj(vec![
                                ("id", Json::Str(al.id)),
                                ("title", Json::Str(al.title)),
                                ("artist", Json::Str(al.artist)),
                                ("year", Json::Num(al.year as f64)),
                                ("track_count", Json::Num(al.track_count as f64)),
                                ("cover_url", Json::Str(al.cover_url)),
                            ]));
                        }
                    }
                }
            }
        }
    }

    // 5. Dislikes count
    let mut dislikes_count: i64 = 0;
    if let Ok(resp) = api_get_json(&auth.token, &format!("/users/{}/dislikes/tracks", auth.uid)) {
        if let Some(arr) = resp
            .get("result")
            .and_then(|r| r.get("library"))
            .and_then(|l| l.get("tracks"))
            .and_then(|v| v.as_arr())
        {
            dislikes_count = arr.len() as i64;
        }
    }

    let result = obj(vec![
        ("likes_count", Json::Num(likes_count as f64)),
        ("dislikes_count", Json::Num(dislikes_count as f64)),
        ("liked_tracks", Json::Arr(liked_tracks_json)),
        ("favorite_artists", Json::Arr(favorite_artists)),
        ("playlists", Json::Arr(playlists)),
        ("liked_albums", Json::Arr(liked_albums)),
    ]);
    if let Ok(mut g) = cache.lock() {
        *g = (auth.uid, now, result.clone());
    }
    Ok(result)
}

pub fn fetch_orbs_json() -> Result<Vec<Json>, String> {
    use std::collections::HashMap;
    use std::sync::{Mutex, OnceLock};
    static ORB_CACHE: OnceLock<Mutex<(i64, Vec<Json>)>> = OnceLock::new();
    let cache = ORB_CACHE.get_or_init(|| Mutex::new((0, Vec::new())));
    let now = now_unix_i64();
    if let Ok(g) = cache.lock() {
        if !g.1.is_empty() && now - g.0 < 600 {
            return Ok(g.1.clone());
        }
    }

    let auth = load_auth().ok_or("Not logged in to Yandex Music")?;
    let mut orbs: Vec<Json> = Vec::new();
    let palette = [
        "#6D4AFF", "#E65C00", "#FF1F8E", "#00C896", "#3D7EFF", "#FF6D00", "#B24BF3"
    ];
    let mut color_idx = 0usize;

    // 1. Extract top favorite artists from user's Liked tracks (with real cover art!)
    if let Ok(liked) = fetch_likes(&auth) {
        let mut counts: HashMap<String, (usize, String)> = HashMap::new();
        for tr in liked.iter().take(60) {
            let main_artist = tr.artist.split(',').next().unwrap_or("").trim();
            if main_artist.is_empty() {
                continue;
            }
            let entry = counts
                .entry(main_artist.to_string())
                .or_insert_with(|| (0, tr.cover_url.clone()));
            entry.0 += 1;
            if entry.1.is_empty() && !tr.cover_url.is_empty() {
                entry.1 = tr.cover_url.clone();
            }
        }
        let mut ranked: Vec<(String, usize, String)> = counts
            .into_iter()
            .map(|(name, (cnt, cov))| (name, cnt, cov))
            .collect();
        ranked.sort_by(|a, b| b.1.cmp(&a.1));
        for (artist, _cnt, cov) in ranked.into_iter().take(4) {
            let col = palette[color_idx % palette.len()];
            color_idx += 1;
            orbs.push(obj(vec![
                ("id", Json::Str(format!("artist:{artist}"))),
                ("title", Json::Str(artist.clone())),
                ("subtitle", Json::Str("Артист".into())),
                ("mode", Json::Str("search".into())),
                ("query", Json::Str(artist)),
                ("cover_url", Json::Str(cov)),
                ("color", Json::Str(col.into())),
            ]));
        }
    }

    // 2. Add personalized Yandex Music dashboard stations (/rotor/stations/dashboard)
    if let Ok(resp) = api_get_json(&auth.token, "/rotor/stations/dashboard") {
        if let Some(arr) = resp
            .get("result")
            .and_then(|r| r.get("stations"))
            .and_then(|v| v.as_arr())
        {
            for item in arr.iter().take(5) {
                let st = item.get("station");
                let st_type = st.and_then(|s| s.get("id")).and_then(|i| i.get_str("type")).unwrap_or("");
                let st_tag = st.and_then(|s| s.get("id")).and_then(|i| i.get_str("tag")).unwrap_or("");
                let name = st.and_then(|s| s.get_str("name")).unwrap_or("").trim();
                if st_type.is_empty() || st_tag.is_empty() || name.is_empty() || st_tag == "onyourwave" {
                    continue;
                }
                let station_id = format!("{st_type}:{st_tag}");
                let bg_color = st
                    .and_then(|s| s.get("icon"))
                    .and_then(|i| i.get_str("backgroundColor"))
                    .filter(|c| c.starts_with('#'))
                    .unwrap_or(palette[color_idx % palette.len()]);
                color_idx += 1;
                let cov = format_cover_url(
                    st.and_then(|s| s.get("fullImageUrl").or_else(|| s.get("icon").and_then(|i| i.get("imageUrl"))))
                        .and_then(|v| v.as_str())
                );
                orbs.push(obj(vec![
                    ("id", Json::Str(format!("station:{station_id}"))),
                    ("title", Json::Str(name.into())),
                    ("subtitle", Json::Str("Волна".into())),
                    ("mode", Json::Str("station".into())),
                    ("query", Json::Str(station_id)),
                    ("cover_url", Json::Str(cov)),
                    ("color", Json::Str(bg_color.into())),
                ]));
            }
        }
    }

    // Always append quick mood orbs if fewer than 6
    let fallbacks = [
        ("activity:party", "Вечериниться", "Настроение", "station", "activity:party", "#00C896"),
        ("activity:workout", "Бегаю и не устаю", "Занятие", "station", "activity:workout", "#4C7CFF"),
        ("likes", "Мне нравится", "Коллекция", "likes", "", "#FF1F8E"),
        ("chart", "Тренды", "Чарт", "chart", "", "#FF4D00"),
    ];
    for (id, title, sub, mode, query, col) in fallbacks {
        if orbs.len() >= 8 {
            break;
        }
        orbs.push(obj(vec![
            ("id", Json::Str(id.into())),
            ("title", Json::Str(title.into())),
            ("subtitle", Json::Str(sub.into())),
            ("mode", Json::Str(mode.into())),
            ("query", Json::Str(query.into())),
            ("cover_url", Json::Str(String::new())),
            ("color", Json::Str(col.into())),
        ]));
    }

    if let Ok(mut g) = cache.lock() {
        *g = (now, orbs.clone());
    }
    Ok(orbs)
}


fn resolve_track_stream_url(auth: &YMusicAuth, track_id: &str) -> Result<String, String> {
    let ep = format!("/tracks/{}/download-info", url_encode(track_id));
    let resp = api_get_json(&auth.token, &ep)?;
    let infos = resp
        .get("result")
        .and_then(|r| r.as_arr())
        .ok_or("Track has no download-info")?;

    // Prefer 192 kbps MP3 for fastest playback startup on speaker, fallback to any MP3 / codec
    let mut best: Option<(&Json, i32)> = None;
    for info in infos {
        let codec = info.get_str("codec").unwrap_or("");
        let is_mp3 = codec.eq_ignore_ascii_case("mp3");
        let bitrate = info.get("bitrateInKbps").and_then(|v| v.as_i64()).unwrap_or(0);
        let score = if is_mp3 {
            match bitrate {
                192 => 100,
                128 => 90,
                320 => 80,
                b if b > 0 => 70,
                _ => 60,
            }
        } else {
            10
        };
        if best.map(|(_, ps)| score > ps).unwrap_or(true) {
            best = Some((info, score));
        }
    }
    let chosen = best.ok_or("No playable audio stream for track")?.0;
    let dl_url = chosen.get_str("downloadInfoUrl").ok_or("Missing downloadInfoUrl")?;
    let codec = chosen.get_str("codec").unwrap_or("mp3");
    let auth_hdr = format!("OAuth {}", auth.token);
    let hdrs = [("Authorization", auth_hdr.as_str()), CLIENT_HEADER, LANG_HEADER];
    let raw = request_bytes(dl_url, None, &hdrs, None, 15, 256 * 1024, None)?;
    let (host, path, ts, s) = parse_download_info_payload(&raw)?;
    Ok(build_stream_url(&host, codec, &path, &ts, &s))
}


fn read_audio_envelope() -> (f64, f64, f64, f64, f64) {
    let Ok(txt) = fs::read_to_string("/var/run/atrios/ymusic_audio.json") else {
        return (0.0, 0.0, 0.0, 0.0, 0.0);
    };
    let Ok(j) = Json::parse(&txt) else {
        return (0.0, 0.0, 0.0, 0.0, 0.0);
    };
    let b = j.get("b").and_then(|v| v.as_f64()).unwrap_or(0.0);
    let v = j.get("v").and_then(|v| v.as_f64()).unwrap_or(0.0);
    let h = j.get("h").and_then(|v| v.as_f64()).unwrap_or(0.0);
    let e = j.get("e").and_then(|v| v.as_f64()).unwrap_or(0.0);
    let p = j.get("p").and_then(|v| v.as_f64()).unwrap_or(0.0);
    (b, v, h, e, p)
}

fn is_track_liked_live(auth: &YMusicAuth, track_id: &str) -> Option<bool> {
    use std::collections::HashSet;
    use std::sync::{Mutex, OnceLock};
    if track_id.is_empty() {
        return None;
    }
    static LIKES_CACHE: OnceLock<Mutex<(i64, HashSet<String>)>> = OnceLock::new();
    let cache = LIKES_CACHE.get_or_init(|| Mutex::new((0, HashSet::new())));
    let now = now_unix_i64();
    if let Ok(g) = cache.lock() {
        if now - g.0 < 8 && !g.1.is_empty() {
            return Some(g.1.contains(track_id));
        }
    }
    let ep = format!("/users/{}/likes/tracks", auth.uid);
    let resp = api_get_json(&auth.token, &ep).ok()?;
    let arr = resp
        .get("result")
        .and_then(|r| r.get("library"))
        .and_then(|l| l.get("tracks"))
        .and_then(|v| v.as_arr())?;
    let mut set = HashSet::with_capacity(arr.len());
    for item in arr {
        match item.get("id") {
            Some(Json::Str(s)) if !s.is_empty() => { set.insert(s.clone()); }
            Some(Json::Num(n)) => { set.insert(format!("{}", *n as i64)); }
            _ => {}
        }
    }
    let found = set.contains(track_id);
    if let Ok(mut g) = cache.lock() {
        *g = (now, set);
    }
    Some(found)
}

fn invalidate_likes_cache() {
    // Handled via short 8s TTL + immediate local toggle
}

fn api_unlike_track(auth: &YMusicAuth, track_id: &str) -> Result<(), String> {
    let ep = format!("/users/{}/likes/tracks/remove", auth.uid);
    let body = format!("track-ids={}", url_encode(track_id));
    let _ = api_post_form_json(&auth.token, &ep, &body)?;
    Ok(())
}

fn api_like_track(auth: &YMusicAuth, track_id: &str, dislike: bool) -> Result<(), String> {
    let ep = if dislike {
        format!("/users/{}/dislikes/tracks/add-multiple", auth.uid)
    } else {
        format!("/users/{}/likes/tracks/add-multiple", auth.uid)
    };
    let body = format!("track-ids={}", url_encode(track_id));
    let _ = api_post_form_json(&auth.token, &ep, &body)?;
    Ok(())
}

// --- Audio Decoder & Playback Pipeline ---
fn bin_exists(name: &str) -> bool {
    Command::new("sh")
        .args(["-c", &format!("command -v {name} >/dev/null 2>&1")])
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

static MP3_RAM_CACHE: std::sync::OnceLock<std::sync::Mutex<std::collections::VecDeque<(String, Vec<u8>)>>> = std::sync::OnceLock::new();

fn mp3_cache_get(track_id: &str) -> Option<Vec<u8>> {
    if track_id.is_empty() { return None; }
    let c = MP3_RAM_CACHE.get_or_init(|| std::sync::Mutex::new(std::collections::VecDeque::new()));
    let g = c.lock().ok()?;
    g.iter().find(|(id, _)| id == track_id).map(|(_, b)| b.clone())
}

fn mp3_cache_put(track_id: String, data: Vec<u8>) {
    if track_id.is_empty() || data.len() < 16384 || data.len() > 18 * 1024 * 1024 { return; }
    let c = MP3_RAM_CACHE.get_or_init(|| std::sync::Mutex::new(std::collections::VecDeque::new()));
    if let Ok(mut g) = c.lock() {
        g.retain(|(id, _)| id != &track_id);
        while g.len() >= 20 {
            g.pop_front();
        }
        g.push_back((track_id, data));
    }
}

struct ActivePipeline {
    decoder: Child,
    sink: Option<Child>,
    paused: bool,
    started_at: std::time::Instant,
}

impl ActivePipeline {
    fn spawn_from_ram(bytes: Vec<u8>) -> Result<Self, String> {
        let _ = Command::new("pkill").args(["-9", "-x", "atri-mp3dec"]).stdout(Stdio::null()).stderr(Stdio::null()).status();
        let mut dec = Command::new("atri-mp3dec")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .map_err(|e| format!("atri-mp3dec spawn failed: {e}"))?;
        let dec_in = dec.stdin.take().ok_or("Missing atri-mp3dec stdin")?;
        let dec_out = dec.stdout.take().ok_or("Missing atri-mp3dec stdout")?;
        let sink = Command::new("aplay")
            .args(["-q", "-B", "100000", "-F", "25000", "-t", "raw", "-f", "S16_LE", "-r", "44100", "-c", "2"])
            .stdin(Stdio::from(dec_out))
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .map_err(|e| { let _ = dec.kill(); format!("aplay spawn failed: {e}") })?;
        thread::spawn(move || {
            use std::io::Write;
            let mut w = dec_in;
            let _ = w.write_all(&bytes);
        });
        Ok(Self { decoder: dec, sink: Some(sink), paused: false, started_at: std::time::Instant::now() })
    }

    fn spawn_with_cache(track_id: &str, url: &str) -> Result<Self, String> {
        if let Some(cached) = mp3_cache_get(track_id) {
            return Self::spawn_from_ram(cached);
        }
        Self::spawn_inner(track_id, url)
    }

    fn spawn_inner(track_id: &str, url: &str) -> Result<Self, String> {
        // Ensure zero overlapping decoders from any previous track
        let _ = Command::new("pkill")
            .args(["-9", "-x", "atri-mp3dec"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        if bin_exists("ffmpeg") {
            let mut dec = Command::new("ffmpeg")
                .args([
                    "-nostdin",
                    "-loglevel",
                    "error",
                    "-reconnect",
                    "1",
                    "-reconnect_streamed",
                    "1",
                    "-reconnect_delay_max",
                    "5",
                    "-i",
                    url,
                    "-f",
                    "s16le",
                    "-ar",
                    "48000",
                    "-ac",
                    "2",
                    "pipe:1",
                ])
                .stdout(Stdio::piped())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| format!("ffmpeg spawn failed: {e}"))?;
            let dec_out = dec.stdout.take().ok_or("Missing ffmpeg stdout")?;
            let sink = Command::new("aplay")
                .args(["-q", "-t", "raw", "-f", "S16_LE", "-r", "48000", "-c", "2"])
                .stdin(Stdio::from(dec_out))
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| {
                    let _ = dec.kill();
                    let _ = dec.wait();
                    format!("aplay spawn failed: {e}")
                })?;
            return Ok(Self { decoder: dec, sink: Some(sink), paused: false, started_at: std::time::Instant::now() });
        }
        if bin_exists("mpg123") {
            let mut dec = Command::new("mpg123")
                .args(["-q", "-s", url])
                .stdout(Stdio::piped())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| format!("mpg123 spawn failed: {e}"))?;
            let dec_out = dec.stdout.take().ok_or("Missing mpg123 stdout")?;
            let sink = Command::new("aplay")
                .args(["-q", "-B", "100000", "-F", "25000", "-t", "raw", "-f", "S16_LE", "-r", "44100", "-c", "2"])
                .stdin(Stdio::from(dec_out))
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| {
                    let _ = dec.kill();
                    let _ = dec.wait();
                    format!("aplay spawn failed: {e}")
                })?;
            return Ok(Self { decoder: dec, sink: Some(sink), paused: false, started_at: std::time::Instant::now() });
        }
        if bin_exists("gst-launch-1.0") {
            let dec = Command::new("gst-launch-1.0")
                .args(["-q", "playbin", &format!("uri={url}")])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| format!("gst-launch-1.0 spawn failed: {e}"))?;
            return Ok(Self { decoder: dec, sink: None, paused: false, started_at: std::time::Instant::now() });
        }
        if bin_exists("madplay") && bin_exists("curl") {
            let dec = Command::new("sh")
                .args([
                    "-c",
                    "curl -fsSL \"$1\" | madplay -q -o raw:- - | aplay -q -t raw -f S16_LE -r 44100 -c 2",
                    "sh",
                    url,
                ])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| format!("madplay pipeline failed: {e}"))?;
            return Ok(Self { decoder: dec, sink: None, paused: false, started_at: std::time::Instant::now() });
        }
        if bin_exists("atri-mp3dec") {
            let mut dec = Command::new("atri-mp3dec")
                .stdin(Stdio::piped())
                .stdout(Stdio::piped())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| format!("atri-mp3dec spawn failed: {e}"))?;
            let dec_in = dec.stdin.take().ok_or("Missing atri-mp3dec stdin")?;
            let dec_out = dec.stdout.take().ok_or("Missing atri-mp3dec stdout")?;
            let sink = Command::new("aplay")
                .args(["-q", "-B", "100000", "-F", "25000", "-t", "raw", "-f", "S16_LE", "-r", "44100", "-c", "2"])
                .stdin(Stdio::from(dec_out))
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|e| {
                    let _ = dec.kill();
                    format!("aplay spawn failed: {e}")
                })?;
            let stream_url = url.to_string();
            let cache_tid = track_id.to_string();
            // Full-speed RAM-buffered feeder + 4-track LRU cache (instant prev switch + zero 1/4 CDN drop!)
            let shared = std::sync::Arc::new((
                std::sync::Mutex::new((std::collections::VecDeque::<Vec<u8>>::new(), false)),
                std::sync::Condvar::new(),
            ));
            let prod_s = shared.clone();
            thread::spawn(move || {
                let mut full_buf: Vec<u8> = Vec::with_capacity(4 * 1024 * 1024);
                let ok = crate::https::request_stream(
                    &stream_url,
                    None,
                    &[],
                    None,
                    120,
                    64 * 1024 * 1024,
                    None,
                    |chunk| {
                        if full_buf.len() + chunk.len() <= 18 * 1024 * 1024 {
                            full_buf.extend_from_slice(chunk);
                        }
                        let (lk, cv) = &*prod_s;
                        let mut g = lk.lock().unwrap();
                        g.0.push_back(chunk.to_vec());
                        cv.notify_one();
                        Ok(())
                    },
                ).is_ok();
                if ok && !cache_tid.is_empty() && !full_buf.is_empty() {
                    mp3_cache_put(cache_tid, full_buf);
                }
                let (lk, cv) = &*prod_s;
                let mut g = lk.lock().unwrap();
                g.1 = true;
                cv.notify_one();
            });
            thread::spawn(move || {
                use std::io::Write;
                let mut w = dec_in;
                let (lk, cv) = &*shared;
                loop {
                    let next_chunk = {
                        let mut g = lk.lock().unwrap();
                        while g.0.is_empty() && !g.1 {
                            g = cv.wait(g).unwrap();
                        }
                        g.0.pop_front()
                    };
                    match next_chunk {
                        Some(buf) => {
                            if w.write_all(&buf).is_err() {
                                break;
                            }
                        }
                        None => break,
                    }
                }
            });
            return Ok(Self {
                decoder: dec,
                sink: Some(sink),
                paused: false,
                started_at: std::time::Instant::now(),
            });
        }
        Err("No audio stream decoder found (install ffmpeg, mpg123, madplay, or atri-mp3dec)".into())
    }

    fn set_paused(&mut self, pause: bool) {
        let sig = if pause { SIGSTOP_NUM } else { SIGCONT_NUM };
        unsafe {
            kill(self.decoder.id() as i32, sig);
            if let Some(ref s) = self.sink {
                kill(s.id() as i32, sig);
            }
        }
        self.paused = pause;
    }

    fn is_finished(&mut self) -> bool {
        if self.paused {
            return false;
        }
        let exited = if let Some(ref mut s) = self.sink {
            matches!(s.try_wait(), Ok(Some(_)))
        } else {
            matches!(self.decoder.try_wait(), Ok(Some(_)))
        };
        if !exited {
            return false;
        }
        // Guard against rapid-skip loops if a stream exits prematurely (< 2.5s)
        self.started_at.elapsed() >= Duration::from_millis(1000)
    }

    fn stop(mut self) {
        if self.paused {
            self.set_paused(false);
        }
        // Gracefully stop ALSA sink FIRST (SIGTERM=15) so PCM closes cleanly without DC pop/click
        if let Some(mut s) = self.sink.take() {
            unsafe {
                kill(s.id() as i32, 15);
            }
            let deadline = std::time::Instant::now() + Duration::from_millis(80);
            while std::time::Instant::now() < deadline {
                if matches!(s.try_wait(), Ok(Some(_))) {
                    break;
                }
                thread::sleep(Duration::from_millis(10));
            }
            unsafe {
                kill(s.id() as i32, SIGKILL_NUM);
            }
            let _ = s.wait();
        }
        unsafe {
            kill(self.decoder.id() as i32, SIGKILL_NUM);
        }
        let _ = self.decoder.wait();
        let _ = Command::new("pkill")
            .args(["-9", "-x", "atri-mp3dec"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        thread::sleep(Duration::from_millis(25));
    }
}

// --- Daemon / Player Loop (Ynison/Sonata-grade async state machine) ---
enum DaemonEvent {
    ModeLoaded {
        gen: u64,
        mode: String,
        station: String,
        batch_id: String,
        start_index: usize,
        res: Result<Vec<TrackInfo>, String>,
    },
    StreamResolved {
        gen: u64,
        track_id: String,
        res: Result<String, String>,
    },
    LikedChecked {
        track_id: String,
        liked: bool,
    },
    WaveAppended {
        batch_id: String,
        tracks: Vec<TrackInfo>,
        auto_play: bool,
    },
}

struct PlayerState {
    mode: String,
    active_station: String,
    batch_id: String,
    queue: Vec<TrackInfo>,
    index: usize,
    pipeline: Option<ActivePipeline>,
    prefetch: Arc<std::sync::Mutex<Option<(String, String)>>>,
    pending_play_at: Option<std::time::Instant>,
    last_skip_at: Option<std::time::Instant>,
    repeat_one: bool,
    load_gen: Arc<AtomicU64>,
    tx: mpsc::Sender<DaemonEvent>,
    status: YMusicStatus,
}

impl PlayerState {
    fn new(tx: mpsc::Sender<DaemonEvent>) -> Self {
        Self {
            mode: "none".into(),
            active_station: "user:onyourwave".into(),
            batch_id: String::new(),
            queue: Vec::new(),
            index: 0,
            pipeline: None,
            prefetch: Arc::new(std::sync::Mutex::new(None)),
            pending_play_at: None,
            last_skip_at: None,
            repeat_one: false,
            load_gen: Arc::new(AtomicU64::new(1)),
            tx,
            status: YMusicStatus::default_idle(),
        }
    }

    fn bump_gen(&self) -> u64 {
        self.load_gen.fetch_add(1, Ordering::SeqCst) + 1
    }

    fn stop_playback(&mut self) {
        self.bump_gen();
        self.pending_play_at = None;
        if let Some(p) = self.pipeline.take() {
            p.stop();
        }
        self.status.state = "idle".into();
        self.status.started_ts = 0;
        self.status.error = None;
        write_status(&mut self.status);
    }

    fn sync_neighbors_to_status(&mut self) {
        if let Some(nxt) = self.queue.get(self.index + 1) {
            self.status.next_title = nxt.title.clone();
            self.status.next_artist = nxt.artist.clone();
            self.status.next_cover_url = nxt.cover_url.clone();
        } else {
            self.status.next_title.clear();
            self.status.next_artist.clear();
            self.status.next_cover_url.clear();
        }
        // Hard stop at first seen track (index == 0): NEVER wrap around backward!
        self.status.can_prev = self.index > 0;
        self.status.can_next = true;
        if self.index > 0 {
            if let Some(prv) = self.queue.get(self.index - 1) {
                self.status.prev_title = prv.title.clone();
                self.status.prev_artist = prv.artist.clone();
                self.status.prev_cover_url = prv.cover_url.clone();
            }
        } else {
            self.status.prev_title.clear();
            self.status.prev_artist.clear();
            self.status.prev_cover_url.clear();
        }
    }

    fn prebuffer_next_in_bg(&self, auth: &YMusicAuth) {
        if let Some(nxt) = self.queue.get(self.index + 1).cloned() {
            if mp3_cache_get(&nxt.id).is_some() {
                return;
            }
            let pf = self.prefetch.clone();
            let auth_c = auth.clone();
            thread::spawn(move || {
                if let Ok(u) = resolve_track_stream_url(&auth_c, &nxt.id) {
                    if let Ok(mut g) = pf.lock() {
                        *g = Some((nxt.id.clone(), u.clone()));
                    }
                    // Pre-download next track MP3 into RAM cache (like Yandex Music mobile) for <40ms skip!
                    if let Ok(bytes) = request_bytes(&u, None, &[], None, 25, 16 * 1024 * 1024, None) {
                        mp3_cache_put(nxt.id, bytes);
                    }
                }
            });
        }
    }

    fn finish_starting_pipeline(&mut self, track: &TrackInfo, auth: &YMusicAuth, pipe_res: Result<ActivePipeline, String>) {
        match pipe_res {
            Ok(pipe) => {
                self.pipeline = Some(pipe);
                self.status.state = "playing".into();
                self.status.position_ms = 0;
                self.status.started_ts = now_unix_ms();
                self.status.error = None;
                write_status(&mut self.status);
                println!("[ymusic] Playing: {} — {}", track.artist, track.title);
                self.prebuffer_next_in_bg(auth);
                if self.mode == "wave" || self.mode == "station" {
                    let auth_c = auth.clone();
                    let st = self.active_station.clone();
                    let bid = self.batch_id.clone();
                    let tid = track.id.clone();
                    thread::spawn(move || {
                        send_rotor_feedback_station(&auth_c, &st, &bid, "trackStarted", Some(&tid), None);
                    });
                }
                // Background live liked check (never blocks bt-control or command loop!)
                let auth_l = auth.clone();
                let tid_l = track.id.clone();
                let tx_l = self.tx.clone();
                thread::spawn(move || {
                    if let Some(liked) = is_track_liked_live(&auth_l, &tid_l) {
                        let _ = tx_l.send(DaemonEvent::LikedChecked { track_id: tid_l, liked });
                    }
                });
            }
            Err(e) => {
                eprintln!("[ymusic] Playback error: {e}");
                self.status.state = "error".into();
                self.status.started_ts = 0;
                self.status.error = Some(e);
                write_status(&mut self.status);
            }
        }
    }

    fn start_current_track(&mut self) {
        let gen = self.bump_gen();
        self.pending_play_at = None;
        if let Some(p) = self.pipeline.take() {
            p.stop();
        }
        let Some(auth) = load_auth() else {
            self.status.logged_in = false;
            self.status.state = "error".into();
            self.status.error = Some("Not logged in to Yandex Music".into());
            write_status(&mut self.status);
            return;
        };
        let Some(track) = self.queue.get(self.index).cloned() else {
            self.status.state = "idle".into();
            write_status(&mut self.status);
            return;
        };
        self.status.logged_in = true;
        self.status.uid = auth.uid;
        self.status.login = auth.login.clone();
        self.status.display_name = auth.display_name.clone();
        self.status.avatar_url = auth.avatar_url.clone();
        self.status.mode = self.mode.clone();
        self.status.state = "loading".into();
        self.status.track_id = track.id.clone();
        self.status.title = track.title.clone();
        self.status.artist = track.artist.clone();
        self.status.album = track.album.clone();
        self.status.cover_url = track.cover_url.clone();
        self.sync_neighbors_to_status();
        self.status.duration_ms = track.duration_ms;
        self.status.position_ms = 0;
        self.status.started_ts = 0;
        self.status.liked = track.liked;
        self.status.error = None;
        write_status(&mut self.status);

        // Fast path: already in RAM cache (prev OR pre-buffered next track) -> instant <30ms start!
        if let Some(cached) = mp3_cache_get(&track.id) {
            let res = ActivePipeline::spawn_from_ram(cached);
            self.finish_starting_pipeline(&track, &auth, res);
            return;
        }

        // Fast path 2: stream URL already prefetched -> spawn immediately!
        let cached_url = self.prefetch.lock().ok().and_then(|mut g| {
            if g.as_ref().map(|(id, _)| id == &track.id).unwrap_or(false) {
                g.take().map(|(_, u)| u)
            } else {
                None
            }
        });
        if let Some(url) = cached_url {
            let res = ActivePipeline::spawn_with_cache(&track.id, &url);
            self.finish_starting_pipeline(&track, &auth, res);
            return;
        }

        // Async path: resolve stream URL on background thread so pause/next/prev are NEVER blocked!
        let tx = self.tx.clone();
        let tid = track.id.clone();
        thread::spawn(move || {
            let res = resolve_track_stream_url(&auth, &tid);
            let _ = tx.send(DaemonEvent::StreamResolved { gen, track_id: tid, res });
        });
    }

    fn load_mode(&mut self, mode: &str, query: Option<&str>) {
        let gen = self.bump_gen();
        self.pending_play_at = None;
        if let Some(p) = self.pipeline.take() {
            p.stop();
        }
        let Some(auth) = load_auth() else {
            self.status.logged_in = false;
            self.status.state = "error".into();
            self.status.error = Some("Not logged in to Yandex Music".into());
            write_status(&mut self.status);
            return;
        };
        self.mode = mode.into();
        self.status.mode = mode.into();
        self.status.state = "loading".into();
        self.status.started_ts = 0;
        self.status.error = None;
        write_status(&mut self.status);

        let mode_s = mode.to_string();
        let query_s = query.map(|q| q.to_string());
        let tx = self.tx.clone();
        thread::spawn(move || {
            let mut batch_id = String::new();
            let mut station = "user:onyourwave".to_string();
            let mut start_index: usize = 0;
            let res = match mode_s.as_str() {
                "wave" => fetch_my_wave(&auth, None).map(|(bid, tr)| {
                    batch_id = bid.clone();
                    send_rotor_feedback(&auth, &bid, "radioStarted", None, None);
                    tr
                }),
                "likes" => fetch_likes(&auth),
                "chart" => fetch_chart(&auth),
                "station" => {
                    let st = query_s.as_deref().unwrap_or("user:onyourwave");
                    station = st.to_string();
                    fetch_rotor_station(&auth, st, None).map(|(bid, tr)| {
                        batch_id = bid.clone();
                        send_rotor_feedback_station(&auth, st, &bid, "radioStarted", None, None);
                        tr
                    })
                }
                "playlist" => fetch_user_playlist(&auth, query_s.as_deref().unwrap_or("")),
                "queue" => fetch_remote_queue(&auth, query_s.as_deref().unwrap_or("")).map(|(tr, idx)| {
                    start_index = idx;
                    tr
                }),
                "search" => fetch_search(&auth, query_s.as_deref().unwrap_or("")),
                "track" => fetch_single_track(&auth, query_s.as_deref().unwrap_or("")),
                "album" => fetch_album_tracks(&auth, query_s.as_deref().unwrap_or("")),
                _ => Err(format!("Unsupported mode: {mode_s}")),
            };
            let _ = tx.send(DaemonEvent::ModeLoaded { gen, mode: mode_s, station, batch_id, start_index, res });
        });
    }

    fn handle_event(&mut self, ev: DaemonEvent) {
        let cur_gen = self.load_gen.load(Ordering::SeqCst);
        match ev {
            DaemonEvent::ModeLoaded { gen, mode, station, batch_id, start_index, res } => {
                if gen != cur_gen {
                    return; // Stale load superseded by pause/next/new mode!
                }
                match res {
                    Ok(tracks) => {
                        self.mode = mode;
                        self.active_station = station;
                        if !batch_id.is_empty() {
                            self.batch_id = batch_id;
                        }
                        self.queue = tracks;
                        self.index = start_index.min(self.queue.len().saturating_sub(1));
                        self.start_current_track();
                    }
                    Err(e) => {
                        eprintln!("[ymusic] Load {mode} failed: {e}");
                        self.status.state = "error".into();
                        self.status.error = Some(e);
                        write_status(&mut self.status);
                    }
                }
            }
            DaemonEvent::StreamResolved { gen, track_id, res } => {
                if gen != cur_gen {
                    return; // User paused or skipped while URL was resolving!
                }
                let Some(auth) = load_auth() else { return; };
                let Some(track) = self.queue.get(self.index).cloned() else { return; };
                if track.id != track_id {
                    return;
                }
                let pipe_res = res.and_then(|url| ActivePipeline::spawn_with_cache(&track.id, &url));
                self.finish_starting_pipeline(&track, &auth, pipe_res);
            }
            DaemonEvent::LikedChecked { track_id, liked } => {
                if self.status.track_id == track_id && self.status.liked != liked {
                    if let Some(cur) = self.queue.get_mut(self.index) {
                        if cur.id == track_id {
                            cur.liked = liked;
                        }
                    }
                    self.status.liked = liked;
                    write_status(&mut self.status);
                }
            }
            DaemonEvent::WaveAppended { batch_id, tracks, auto_play } => {
                if !tracks.is_empty() {
                    self.batch_id = batch_id;
                    let was_empty = self.queue.is_empty();
                    self.queue.extend(tracks);
                    self.sync_neighbors_to_status();
                    if auto_play && (was_empty || self.status.state == "loading") {
                        self.start_current_track();
                    } else {
                        write_status(&mut self.status);
                    }
                }
            }
        }
    }

    fn preview_current_track(&mut self) {
        self.bump_gen();
        if let Some(p) = self.pipeline.take() {
            p.stop();
        }
        let Some(track) = self.queue.get(self.index).cloned() else {
            return;
        };
        if let Some(auth) = load_auth() {
            self.status.logged_in = true;
            self.status.uid = auth.uid;
            self.status.login = auth.login;
            self.status.display_name = auth.display_name;
            self.status.avatar_url = auth.avatar_url;
        }
        self.status.mode = if self.mode == "none" { "wave".into() } else { self.mode.clone() };
        self.status.state = "paused".into();
        self.status.track_id = track.id;
        self.status.title = track.title;
        self.status.artist = track.artist;
        self.status.album = track.album;
        self.status.cover_url = track.cover_url;
        self.sync_neighbors_to_status();
        self.status.duration_ms = track.duration_ms;
        self.status.position_ms = 0;
        self.status.started_ts = 0;
        self.status.liked = track.liked;
        self.status.error = None;
        write_status(&mut self.status);
    }

    fn next_track(&mut self, skipped: bool) {
        let should_play = !skipped
            || self.pending_play_at.is_some()
            || self.status.state == "loading"
            || self.pipeline.as_ref().map(|p| !p.paused).unwrap_or(false);
        if let Some(auth) = load_auth() {
            if self.mode == "wave" || self.mode == "station" {
                if let Some(cur) = self.queue.get(self.index).cloned() {
                    let st = self.active_station.clone();
                    let bid = self.batch_id.clone();
                    let kind = if skipped { "skip" } else { "trackFinished" };
                    let played_ms = if self.status.started_ts > 0 && self.status.state == "playing" {
                        self.status.position_ms + (now_unix_ms() - self.status.started_ts).max(0)
                    } else if !skipped && cur.duration_ms > 0 {
                        cur.duration_ms
                    } else {
                        self.status.position_ms
                    };
                    let played_sec = (played_ms as f64 / 1000.0).max(0.0);
                    thread::spawn(move || {
                        send_rotor_feedback_station(&auth, &st, &bid, kind, Some(&cur.id), Some(played_sec));
                    });
                }
            }
        }
        if self.queue.is_empty() {
            self.load_mode("wave", None);
            return;
        }
        if self.index + 1 < self.queue.len() {
            self.index += 1;
            // Keep up to 20 seen/played tracks in history so user can swipe back/forward freely
            if self.index > 20 {
                let drop_n = self.index - 20;
                self.queue.drain(0..drop_n);
                self.index -= drop_n;
            }
            // Proactively replenish My Wave / Station near queue tail (like Yandex Music mobile!)
            if (self.mode == "wave" || self.mode == "station") && self.index + 2 >= self.queue.len() {
                if let Some(auth) = load_auth() {
                    let st = self.active_station.clone();
                    let last_id = self.queue.last().map(|t| t.id.clone());
                    let tx = self.tx.clone();
                    thread::spawn(move || {
                        if let Ok((bid, more)) = fetch_rotor_station(&auth, &st, last_id.as_deref()) {
                            let _ = tx.send(DaemonEvent::WaveAppended { batch_id: bid, tracks: more, auto_play: false });
                        }
                    });
                }
            }
            if should_play {
                self.start_current_track();
            } else {
                self.preview_current_track();
            }
            return;
        }
        if self.mode == "wave" || self.mode == "none" {
            self.load_mode("wave", None);
            return;
        } else if !self.queue.is_empty() {
            self.index = 0;
            if should_play {
                self.start_current_track();
            } else {
                self.preview_current_track();
            }
            return;
        }
        self.stop_playback();
    }

    fn prev_track(&mut self) {
        // Hard stop at first seen track (index == 0): cannot go back further!
        if self.queue.is_empty() || self.index == 0 {
            self.sync_neighbors_to_status();
            write_status(&mut self.status);
            return;
        }
        let should_play = self.pending_play_at.is_some()
            || self.status.state == "loading"
            || self.pipeline.as_ref().map(|p| !p.paused).unwrap_or(false);
        self.index -= 1;
        if should_play {
            self.start_current_track();
        } else {
            self.preview_current_track();
        }
    }

    fn handle_command(&mut self, line: &str) {
        let cmd = line.trim();
        if cmd == "play:wave" {
            self.load_mode("wave", None);
        } else if cmd == "play:likes" {
            self.load_mode("likes", None);
        } else if cmd == "play:chart" {
            self.load_mode("chart", None);
        } else if let Some(st) = cmd.strip_prefix("play:station:") {
            self.load_mode("station", Some(st));
        } else if let Some(pl) = cmd.strip_prefix("play:playlist:") {
            self.load_mode("playlist", Some(pl));
        } else if let Some(tid) = cmd.strip_prefix("play:track:") {
            self.load_mode("track", Some(tid));
        } else if let Some(aid) = cmd.strip_prefix("play:album:") {
            self.load_mode("album", Some(aid));
        } else if let Some(q) = cmd.strip_prefix("play:search:") {
            self.load_mode("search", Some(q));
        } else if cmd == "pause" {
            self.pending_play_at = None;
            if self.status.state == "loading" {
                self.bump_gen();
            }
            if let Some(ref mut p) = self.pipeline {
                if !p.paused {
                    p.set_paused(true);
                }
            }
            if self.status.started_ts > 0 {
                let now_ms = now_unix_ms();
                let start_ms = if self.status.started_ts > 10_000_000_000 { self.status.started_ts } else { self.status.started_ts * 1000 };
                let elapsed = (now_ms - start_ms).max(0);
                self.status.position_ms = (self.status.position_ms + elapsed).clamp(0, self.status.duration_ms.max(0));
                self.status.started_ts = 0;
            }
            self.status.state = "paused".into();
            write_status(&mut self.status);
        } else if cmd == "play" || cmd == "resume" {
            if let Some(ref mut p) = self.pipeline {
                if p.paused {
                    p.set_paused(false);
                    self.status.started_ts = now_unix_ms();
                    self.status.state = "playing".into();
                    write_status(&mut self.status);
                }
            } else if !self.queue.is_empty() {
                self.start_current_track();
            } else if load_auth().is_some() {
                let m = if matches!(self.mode.as_str(), "likes" | "chart") { self.mode.clone() } else { "wave".into() };
                self.load_mode(&m, None);
            }
        } else if cmd == "toggle" {
            let is_paused = self.status.state != "playing" && self.status.state != "loading";
            if is_paused {
                self.handle_command("play");
            } else {
                self.handle_command("pause");
            }
        } else if cmd == "next" {
            if self.last_skip_at.map(|t| t.elapsed() < Duration::from_millis(1000)).unwrap_or(false) {
                return;
            }
            self.last_skip_at = Some(std::time::Instant::now());
            self.next_track(true);
        } else if cmd == "prev" {
            if self.last_skip_at.map(|t| t.elapsed() < Duration::from_millis(250)).unwrap_or(false) {
                return;
            }
            self.last_skip_at = Some(std::time::Instant::now());
            self.prev_track();
        } else if cmd == "like" {
            if let (Some(auth), Some(cur)) = (load_auth(), self.queue.get_mut(self.index)) {
                let want_like = !self.status.liked;
                let tid = cur.id.clone();
                cur.liked = want_like;
                self.status.liked = want_like;
                write_status(&mut self.status);
                thread::spawn(move || {
                    if want_like {
                        let _ = api_like_track(&auth, &tid, false);
                    } else {
                        let _ = api_unlike_track(&auth, &tid);
                    }
                });
            }
        } else if cmd == "dislike" {
            if let (Some(auth), Some(cur)) = (load_auth(), self.queue.get(self.index).cloned()) {
                thread::spawn(move || {
                    let _ = api_like_track(&auth, &cur.id, true);
                });
            }
            self.next_track(true);
        } else if cmd == "repeat" {
            self.repeat_one = !self.repeat_one;
            self.status.repeat = self.repeat_one;
            write_status(&mut self.status);
        } else if cmd == "stop" {
            self.stop_playback();
        } else if cmd == "shutdown" {
            self.stop_playback();
            STOP_REQUESTED.store(true, Ordering::SeqCst);
        }
    }
}

fn run_daemon() -> i32 {
    let _ = fs::create_dir_all("/var/run/atrios");
    let _lock_file = match fs::OpenOptions::new()
        .create(true)
        .write(true)
        .open("/var/run/atrios/ymusic.lock")
    {
        Ok(f) => {
            use std::os::unix::io::AsRawFd;
            if unsafe { crate::sys::flock(f.as_raw_fd(), crate::sys::LOCK_EX | crate::sys::LOCK_NB) } != 0 {
                eprintln!("[ymusic] daemon already running, exiting duplicate");
                return 0;
            }
            f
        }
        Err(e) => {
            eprintln!("[ymusic] lock open failed: {e}");
            return 1;
        }
    };
    unsafe {
        signal(SIGTERM, on_stop as *const () as usize);
        signal(SIGINT, on_stop as *const () as usize);
    }
    if let Some(p) = Path::new(YMUSIC_CMD_SOCK).parent() {
        let _ = fs::create_dir_all(p);
    }
    let _ = fs::remove_file(YMUSIC_CMD_SOCK);
    let sock = match UnixDatagram::bind(YMUSIC_CMD_SOCK) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("[ymusic] bind {YMUSIC_CMD_SOCK} failed: {e}");
            return 1;
        }
    };
    let _ = sock.set_read_timeout(Some(Duration::from_millis(25)));

    let (tx, rx) = mpsc::channel::<DaemonEvent>();
    let mut player = PlayerState::new(tx);
    write_status(&mut player.status);
    let mut buf = [0u8; 2048];

    while !STOP_REQUESTED.load(Ordering::SeqCst) {
        if let Ok(n) = sock.recv(&mut buf) {
            if n > 0 {
                let line = String::from_utf8_lossy(&buf[..n]).to_string();
                player.handle_command(&line);
            }
        }
        while let Ok(ev) = rx.try_recv() {
            player.handle_event(ev);
        }
        if let Some(due) = player.pending_play_at {
            if std::time::Instant::now() >= due {
                player.pending_play_at = None;
                player.start_current_track();
            }
        }
        let finished = player.pipeline.as_mut().map(|p| p.is_finished()).unwrap_or(false);
        if finished {
            if player.repeat_one {
                player.start_current_track();
            } else {
                player.next_track(false);
            }
        }
    }
    player.stop_playback();
    let _ = fs::remove_file(YMUSIC_CMD_SOCK);
    0
}

fn send_cmd_only(cmd: &str) -> Result<(), String> {
    if !Path::new(YMUSIC_CMD_SOCK).exists() {
        return Err("Yandex Music daemon not running".into());
    }
    let sock = UnixDatagram::unbound().map_err(|e| e.to_string())?;
    sock.send_to(cmd.as_bytes(), YMUSIC_CMD_SOCK)
        .map(|_| ())
        .map_err(|e| e.to_string())
}

fn send_or_spawn_command(cmd: &str) -> Result<(), String> {
    if send_cmd_only(cmd).is_ok() {
        return Ok(());
    }
    let bin = if Path::new("/usr/lib/atrios/atrios-runtime").exists() {
        "/usr/lib/atrios/atrios-runtime"
    } else {
        "/usr/lib/atrios/ota"
    };
    Command::new(bin)
        .args(["ymusic", "daemon"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|e| format!("Failed starting ymusic daemon: {e}"))?;
    for _ in 0..20 {
        thread::sleep(Duration::from_millis(50));
        if send_cmd_only(cmd).is_ok() {
            return Ok(());
        }
    }
    Err("Timed out waiting for ymusic daemon".into())
}

pub fn trigger_play(mode: &str, query: Option<&str>) -> Result<YMusicStatus, String> {
    if load_auth().is_none() {
        return Err("Not logged in to Yandex Music — sign in via AtriOS Android app".into());
    }
    let wire = match mode {
        "wave" => "play:wave".to_string(),
        "likes" => "play:likes".to_string(),
        "chart" => "play:chart".to_string(),
        "station" => format!("play:station:{}", query.unwrap_or("user:onyourwave").trim()),
        "playlist" => format!("play:playlist:{}", query.unwrap_or("").trim()),
        "track" => {
            let tid = query.unwrap_or("").trim();
            if tid.is_empty() {
                return Err("Missing track_id".into());
            }
            format!("play:track:{tid}")
        }
        "album" => {
            let aid = query.unwrap_or("").trim();
            if aid.is_empty() {
                return Err("Missing album_id".into());
            }
            format!("play:album:{aid}")
        }
        "search" => {
            let q = query.unwrap_or("").trim();
            if q.is_empty() {
                return Err("Missing search query".into());
            }
            format!("play:search:{q}")
        }
        _ => return Err(format!("Unsupported play mode: {mode}")),
    };
    let mut st = read_status();
    st.mode = mode.into();
    st.state = "loading".into();
    st.error = None;
    write_status(&mut st);
    send_or_spawn_command(&wire)?;
    Ok(st)
}

pub fn trigger_control(action: &str) -> Result<YMusicStatus, String> {
    if !matches!(action, "play" | "pause" | "toggle" | "next" | "prev" | "like" | "dislike" | "repeat" | "stop") {
        return Err(format!("Unsupported ymusic action: {action}"));
    }
    if action == "stop" {
        let _ = send_cmd_only("stop");
        let mut st = read_status();
        st.state = "idle".into();
        write_status(&mut st);
        return Ok(st);
    }
    let before = read_status();
    send_or_spawn_command(action)?;
    // Wait up to 90ms for daemon to apply the command and bump seq so reply never carries stale pre-command state!
    for _ in 0..9 {
        thread::sleep(Duration::from_millis(10));
        let cur = read_status();
        if cur.seq > before.seq || cur.updated_ms > before.updated_ms {
            return Ok(cur);
        }
    }
    Ok(read_status())
}

// --- Voice Command Parser ---
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum YMusicVoiceCmd {
    PlayWave,
    PlayLikes,
    PlayChart,
    PlayStation(String),
    PlayPlaylist(String),
    PlaySearch(String),
    Like,
    Dislike,
    NowPlaying,
}

fn clean_words(text: &str, wake_word: &str) -> Vec<String> {
    let wake_low = wake_word.trim().to_lowercase();
    let mut words: Vec<String> = text
        .to_lowercase()
        .replace('ё', "е")
        .split(|c: char| !c.is_alphanumeric())
        .filter(|s| !s.is_empty())
        .map(|s| s.to_string())
        .collect();
    // Strip leading wake word / greeting tokens
    while let Some(first) = words.first() {
        if (!wake_low.is_empty() && first == &wake_low)
            || matches!(
                first.as_str(),
                "атри" | "арти" | "атриум" | "привет" | "слушай" | "эй" | "окей" | "ок"
            )
        {
            words.remove(0);
        } else {
            break;
        }
    }
    words
}

pub fn parse_ymusic_voice_command(text: &str, wake_word: &str) -> Option<YMusicVoiceCmd> {
    let words = clean_words(text, wake_word);
    if words.is_empty() {
        return None;
    }
    let joined = words.join(" ");

    // "что сейчас играет" / "что играет" / "какая песня" / "какой трек"
    if matches!(
        joined.as_str(),
        "что сейчас играет"
            | "что играет"
            | "какая песня"
            | "какая песня играет"
            | "какой трек"
            | "какой трек играет"
            | "что за песня"
            | "что за трек"
    ) {
        return Some(YMusicVoiceCmd::NowPlaying);
    }

    // Dislike BEFORE like so "не нравится" doesn't match "нравится"
    if words.iter().any(|w| w == "дизлайк")
        || words.windows(2).any(|w| w[0] == "не" && w[1] == "нравится")
        || joined.contains("плохой трек")
    {
        return Some(YMusicVoiceCmd::Dislike);
    }
    if words.iter().any(|w| matches!(w.as_str(), "лайк" | "лайкни"))
        || joined == "нравится"
        || joined == "мне нравится"
        || joined == "поставь лайк"
    {
        return Some(YMusicVoiceCmd::Like);
    }

    // My Wave / Yandex Music
    if joined.contains("мою волну")
        || joined.contains("моя волна")
        || joined.contains("яндекс музык")
        || joined.contains("яндекс волн")
    {
        return Some(YMusicVoiceCmd::PlayWave);
    }

    // Liked tracks
    if joined.contains("любимые треки")
        || joined.contains("любимые песни")
        || joined.contains("любимую музыку")
        || joined.contains("мои любимые")
    {
        return Some(YMusicVoiceCmd::PlayLikes);
    }

    // Top chart (MarshalX /landing3/chart)
    if joined.contains("чарт")
        || joined.contains("топ треков")
        || joined.contains("популярную музыку")
        || joined.contains("хиты")
    {
        return Some(YMusicVoiceCmd::PlayChart);
    }

    // Genre & mood rotor stations (MarshalX /rotor/station/*)
    if joined.contains("включи рок") || joined.contains("поставь рок") {
        return Some(YMusicVoiceCmd::PlayStation("genre:rock".into()));
    }
    if joined.contains("включи поп") || joined.contains("поставь поп") {
        return Some(YMusicVoiceCmd::PlayStation("genre:pop".into()));
    }
    if joined.contains("включи электроник") || joined.contains("электронную музыку") {
        return Some(YMusicVoiceCmd::PlayStation("genre:electronic".into()));
    }
    if joined.contains("включи рэп") || joined.contains("поставь рэп") {
        return Some(YMusicVoiceCmd::PlayStation("genre:rap".into()));
    }
    if joined.contains("спокойную музыку") || joined.contains("расслабляющую музыку") {
        return Some(YMusicVoiceCmd::PlayStation("mood:calm".into()));
    }
    if joined.contains("энергичную музыку") || joined.contains("бодрую музыку") {
        return Some(YMusicVoiceCmd::PlayStation("mood:energetic".into()));
    }
    if joined.contains("для тренировки") || joined.contains("для спорта") {
        return Some(YMusicVoiceCmd::PlayStation("activity:workout".into()));
    }

    // User playlists (MarshalX /users/{uid}/playlists/list)
    if let Some(rest) = joined.strip_prefix("включи плейлист ") {
        return Some(YMusicVoiceCmd::PlayPlaylist(rest.trim().to_string()));
    }
    if let Some(rest) = joined.strip_prefix("поставь плейлист ") {
        return Some(YMusicVoiceCmd::PlayPlaylist(rest.trim().to_string()));
    }

    // "поставь <песня>" / "включи песню <...>" / "включи трек <...>"
    let search_tail = if let Some(rest) = joined.strip_prefix("поставь песню ") {
        Some(rest)
    } else if let Some(rest) = joined.strip_prefix("поставь трек ") {
        Some(rest)
    } else if let Some(rest) = joined.strip_prefix("включи песню ") {
        Some(rest)
    } else if let Some(rest) = joined.strip_prefix("включи трек ") {
        Some(rest)
    } else if let Some(rest) = joined.strip_prefix("поставь ") {
        // Avoid colliding with timer/alarm/pause/volume/like
        let first = rest.split_whitespace().next().unwrap_or("");
        if matches!(
            first,
            "таймер" | "будильник" | "на" | "паузу" | "громкость" | "звук" | "лайк" | "дизлайк"
        ) {
            None
        } else {
            Some(rest)
        }
    } else {
        None
    };

    if let Some(q) = search_tail {
        let q = q.trim();
        if !q.is_empty() && q != "музыку" {
            return Some(YMusicVoiceCmd::PlaySearch(q.to_string()));
        }
    }

    None
}

pub fn execute_voice_command(cmd: YMusicVoiceCmd) {
    match cmd {
        YMusicVoiceCmd::PlayWave => {
            if let Err(e) = trigger_play("wave", None) {
                eprintln!("[ymusic] wave failed: {e}");
            }
        }
        YMusicVoiceCmd::PlayLikes => {
            if let Err(e) = trigger_play("likes", None) {
                eprintln!("[ymusic] likes failed: {e}");
            }
        }
        YMusicVoiceCmd::PlayChart => {
            if let Err(e) = trigger_play("chart", None) {
                eprintln!("[ymusic] chart failed: {e}");
            }
        }
        YMusicVoiceCmd::PlayStation(st) => {
            if let Err(e) = trigger_play("station", Some(&st)) {
                eprintln!("[ymusic] station {st} failed: {e}");
            }
        }
        YMusicVoiceCmd::PlayPlaylist(pl) => {
            if let Err(e) = trigger_play("playlist", Some(&pl)) {
                eprintln!("[ymusic] playlist {pl} failed: {e}");
            }
        }
        YMusicVoiceCmd::PlaySearch(q) => {
            if let Err(e) = trigger_play("search", Some(&q)) {
                eprintln!("[ymusic] search failed: {e}");
            }
        }
        YMusicVoiceCmd::Like => {
            let _ = trigger_control("like");
        }
        YMusicVoiceCmd::Dislike => {
            let _ = trigger_control("dislike");
        }
        YMusicVoiceCmd::NowPlaying => {
            announce_now_playing();
        }
    }
}

pub fn run(args: &[String]) -> i32 {
    let cmd = args.first().map(|s| s.as_str()).unwrap_or("status");
    match cmd {
        "daemon" => run_daemon(),
        "status" => {
            println!("{}", read_status_json().to_string_pretty());
            0
        }
        "login" if args.len() >= 2 => match login_with_token(&args[1]) {
            Ok(st) => {
                println!("{}", st.to_json().to_string_pretty());
                0
            }
            Err(e) => {
                eprintln!("[ymusic] login error: {e}");
                1
            }
        },
        "logout" => {
            println!("{}", logout().to_json().to_string_pretty());
            0
        }
        "wave" => match trigger_play("wave", None) {
            Ok(st) => {
                println!("{}", st.to_json().to_string_pretty());
                0
            }
            Err(e) => {
                eprintln!("[ymusic] {e}");
                1
            }
        },
        "likes" => match trigger_play("likes", None) {
            Ok(st) => {
                println!("{}", st.to_json().to_string_pretty());
                0
            }
            Err(e) => {
                eprintln!("[ymusic] {e}");
                1
            }
        },
        "search" if args.len() >= 2 => {
            let q = args[1..].join(" ");
            match trigger_play("search", Some(&q)) {
                Ok(st) => {
                    println!("{}", st.to_json().to_string_pretty());
                    0
                }
                Err(e) => {
                    eprintln!("[ymusic] {e}");
                    1
                }
            }
        }
        "play" | "pause" | "toggle" | "next" | "prev" | "like" | "dislike" | "stop" => {
            match trigger_control(cmd) {
                Ok(st) => {
                    println!("{}", st.to_json().to_string_pretty());
                    0
                }
                Err(e) => {
                    eprintln!("[ymusic] {e}");
                    1
                }
            }
        }
        "now" => {
            announce_now_playing();
            0
        }
        _ => {
            println!("Usage: atrios-runtime ymusic [status|login <token>|logout|wave|likes|search <query>|play|pause|next|prev|like|dislike|stop|now|daemon]");
            1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn md5_known_vectors_and_ymusic_stream_sign() {
        assert_eq!(md5_hex(b""), "d41d8cd98f00b204e9800998ecf8427e");
        assert_eq!(md5_hex(b"abc"), "900150983cd24fb0d6963f7d28e17f72");
        assert_eq!(
            md5_hex(b"message digest"),
            "f96b697d7cb7938d525a2f31aaf161d0"
        );
        // Verify Yandex Music sign strips leading '/' and prepends salt
        let s1 = ymusic_stream_sign("/get-mp3/abc/123", "secret");
        let s2 = ymusic_stream_sign("get-mp3/abc/123", "secret");
        assert_eq!(s1, s2);
        assert_eq!(s1, md5_hex(b"XGRlBW9FXlekgbPrRHuSiAget-mp3/abc/123secret"));

        let url = build_stream_url("s123.storage.yandex.net", "mp3", "/rmusic/U2FsdGVk/track.mp3", "0005f", "salt123");
        assert!(url.starts_with("https://s123.storage.yandex.net/get-mp3/"));
        assert!(url.ends_with("/0005f/rmusic/U2FsdGVk/track.mp3"));
    }

    #[test]
    fn download_info_xml_and_json_parsing() {
        let xml = b"<download-info><host>s1.yandex.net</host><path>/rmusic/a.mp3</path><ts>12345</ts><s>abcde</s></download-info>";
        assert_eq!(
            parse_download_info_payload(xml).unwrap(),
            ("s1.yandex.net".into(), "/rmusic/a.mp3".into(), "12345".into(), "abcde".into())
        );
        let js = br#"{"host":"s2.yandex.net","path":"/rmusic/b.mp3","ts":"67890","s":"xyz"}"#;
        assert_eq!(
            parse_download_info_payload(js).unwrap(),
            ("s2.yandex.net".into(), "/rmusic/b.mp3".into(), "67890".into(), "xyz".into())
        );
    }

    #[test]
    fn ymusic_voice_command_parsing() {
        assert_eq!(
            parse_ymusic_voice_command("Атри включи мою волну", "Atri"),
            Some(YMusicVoiceCmd::PlayWave)
        );
        assert_eq!(
            parse_ymusic_voice_command("Атри включи яндекс музыку", "Atri"),
            Some(YMusicVoiceCmd::PlayWave)
        );
        assert_eq!(
            parse_ymusic_voice_command("включи любимые треки", "Atri"),
            Some(YMusicVoiceCmd::PlayLikes)
        );
        assert_eq!(
            parse_ymusic_voice_command("Атри поставь кино группа крови", "Atri"),
            Some(YMusicVoiceCmd::PlaySearch("кино группа крови".into()))
        );
        assert_eq!(
            parse_ymusic_voice_command("лайк", "Atri"),
            Some(YMusicVoiceCmd::Like)
        );
        assert_eq!(
            parse_ymusic_voice_command("мне нравится", "Atri"),
            Some(YMusicVoiceCmd::Like)
        );
        assert_eq!(
            parse_ymusic_voice_command("дизлайк", "Atri"),
            Some(YMusicVoiceCmd::Dislike)
        );
        assert_eq!(
            parse_ymusic_voice_command("не нравится", "Atri"),
            Some(YMusicVoiceCmd::Dislike)
        );
        assert_eq!(
            parse_ymusic_voice_command("Атри что сейчас играет", "Atri"),
            Some(YMusicVoiceCmd::NowPlaying)
        );
        // Must NOT hijack timer / alarm / pause
        assert_eq!(parse_ymusic_voice_command("поставь таймер на 5 минут", "Atri"), None);
        assert_eq!(parse_ymusic_voice_command("поставь будильник на 7 утра", "Atri"), None);
        assert_eq!(parse_ymusic_voice_command("поставь на паузу", "Atri"), None);
    }

    #[test]
    fn ymusic_status_json_contract_fields() {
        let st = YMusicStatus::default_idle();
        let j = st.to_json();
        for key in [
            "logged_in",
            "login",
            "display_name",
            "uid",
            "state",
            "mode",
            "track_id",
            "title",
            "artist",
            "album",
            "duration_ms",
            "liked",
            "error",
            "updated_ts",
        ] {
            assert!(j.get(key).is_some(), "missing contracted key: {key}");
        }
    }
}
