use std::collections::BTreeMap;
use std::ffi::{CStr, CString};
use std::net::{IpAddr, TcpStream, ToSocketAddrs};
use std::os::raw::{c_char, c_int, c_long, c_void};
use std::os::unix::io::AsRawFd;
use std::sync::OnceLock;
use std::time::{Duration, Instant};

use crate::json::Json;
use crate::sys::{dlopen, dlsym, poll, PollFd, POLLIN, POLLOUT, RTLD_GLOBAL, RTLD_NOW};

const SSL_VERIFY_PEER: c_int = 1;
const SSL_CTRL_SET_TLSEXT_HOSTNAME: c_int = 55;
const TLSEXT_NAMETYPE_HOST_NAME: c_long = 0;
const SSL_ERROR_WANT_READ: c_int = 2;
const SSL_ERROR_WANT_WRITE: c_int = 3;
const SSL_ERROR_ZERO_RETURN: c_int = 6;

#[allow(non_snake_case)]
struct OpenSslFns {
    TLS_client_method: unsafe extern "C" fn() -> *const c_void,
    SSL_CTX_new: unsafe extern "C" fn(*const c_void) -> *mut c_void,
    SSL_CTX_free: unsafe extern "C" fn(*mut c_void),
    SSL_CTX_set_verify: unsafe extern "C" fn(*mut c_void, c_int, *const c_void),
    SSL_CTX_load_verify_locations:
        unsafe extern "C" fn(*mut c_void, *const c_char, *const c_char) -> c_int,
    SSL_CTX_set_default_verify_paths: unsafe extern "C" fn(*mut c_void) -> c_int,
    SSL_new: unsafe extern "C" fn(*mut c_void) -> *mut c_void,
    SSL_free: unsafe extern "C" fn(*mut c_void),
    SSL_set_fd: unsafe extern "C" fn(*mut c_void, c_int) -> c_int,
    SSL_ctrl: unsafe extern "C" fn(*mut c_void, c_int, c_long, *mut c_void) -> c_long,
    SSL_get0_param: unsafe extern "C" fn(*mut c_void) -> *mut c_void,
    SSL_connect: unsafe extern "C" fn(*mut c_void) -> c_int,
    SSL_read: unsafe extern "C" fn(*mut c_void, *mut c_void, c_int) -> c_int,
    SSL_write: unsafe extern "C" fn(*mut c_void, *const c_void, c_int) -> c_int,
    SSL_shutdown: unsafe extern "C" fn(*mut c_void) -> c_int,
    SSL_get_error: unsafe extern "C" fn(*const c_void, c_int) -> c_int,
    SSL_get_verify_result: unsafe extern "C" fn(*const c_void) -> c_long,
    X509_VERIFY_PARAM_set1_host:
        unsafe extern "C" fn(*mut c_void, *const c_char, usize) -> c_int,
    X509_VERIFY_PARAM_set1_ip_asc: unsafe extern "C" fn(*mut c_void, *const c_char) -> c_int,
    ERR_get_error: unsafe extern "C" fn() -> u64,
    ERR_error_string_n: unsafe extern "C" fn(u64, *mut c_char, usize),
}

static OSSL: OnceLock<Result<OpenSslFns, String>> = OnceLock::new();

unsafe fn sym<T>(h1: *mut c_void, h2: *mut c_void, name: &str) -> Result<T, String> {
    let cname = CString::new(name).unwrap();
    let mut ptr = dlsym(h1, cname.as_ptr());
    if ptr.is_null() && !h2.is_null() {
        ptr = dlsym(h2, cname.as_ptr());
    }
    if ptr.is_null() {
        return Err(format!("Missing OpenSSL symbol {name}"));
    }
    Ok(std::mem::transmute_copy(&ptr))
}

fn load_openssl() -> Result<&'static OpenSslFns, String> {
    let res = OSSL.get_or_init(|| unsafe {
        let mut hcrypto = std::ptr::null_mut();
        for cand in ["libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so"] {
            let c = CString::new(cand).unwrap();
            hcrypto = dlopen(c.as_ptr(), RTLD_NOW | RTLD_GLOBAL);
            if !hcrypto.is_null() {
                break;
            }
        }
        let mut hssl = std::ptr::null_mut();
        for cand in ["libssl.so.3", "libssl.so.1.1", "libssl.so"] {
            let c = CString::new(cand).unwrap();
            hssl = dlopen(c.as_ptr(), RTLD_NOW | RTLD_GLOBAL);
            if !hssl.is_null() {
                break;
            }
        }
        if hssl.is_null() {
            return Err("Failed to load libssl.so".into());
        }
        Ok(OpenSslFns {
            TLS_client_method: sym(hssl, hcrypto, "TLS_client_method")?,
            SSL_CTX_new: sym(hssl, hcrypto, "SSL_CTX_new")?,
            SSL_CTX_free: sym(hssl, hcrypto, "SSL_CTX_free")?,
            SSL_CTX_set_verify: sym(hssl, hcrypto, "SSL_CTX_set_verify")?,
            SSL_CTX_load_verify_locations: sym(hssl, hcrypto, "SSL_CTX_load_verify_locations")?,
            SSL_CTX_set_default_verify_paths: sym(
                hssl,
                hcrypto,
                "SSL_CTX_set_default_verify_paths",
            )?,
            SSL_new: sym(hssl, hcrypto, "SSL_new")?,
            SSL_free: sym(hssl, hcrypto, "SSL_free")?,
            SSL_set_fd: sym(hssl, hcrypto, "SSL_set_fd")?,
            SSL_ctrl: sym(hssl, hcrypto, "SSL_ctrl")?,
            SSL_get0_param: sym(hssl, hcrypto, "SSL_get0_param")?,
            SSL_connect: sym(hssl, hcrypto, "SSL_connect")?,
            SSL_read: sym(hssl, hcrypto, "SSL_read")?,
            SSL_write: sym(hssl, hcrypto, "SSL_write")?,
            SSL_shutdown: sym(hssl, hcrypto, "SSL_shutdown")?,
            SSL_get_error: sym(hssl, hcrypto, "SSL_get_error")?,
            SSL_get_verify_result: sym(hssl, hcrypto, "SSL_get_verify_result")?,
            X509_VERIFY_PARAM_set1_host: sym(hcrypto, hssl, "X509_VERIFY_PARAM_set1_host")?,
            X509_VERIFY_PARAM_set1_ip_asc: sym(hcrypto, hssl, "X509_VERIFY_PARAM_set1_ip_asc")?,
            ERR_get_error: sym(hcrypto, hssl, "ERR_get_error")?,
            ERR_error_string_n: sym(hcrypto, hssl, "ERR_error_string_n")?,
        })
    });
    res.as_ref().map_err(|e| e.clone())
}

fn ossl_err(fns: &OpenSslFns) -> String {
    unsafe {
        let code = (fns.ERR_get_error)();
        if code == 0 {
            return "TLS error".into();
        }
        let mut buf = [0 as c_char; 256];
        (fns.ERR_error_string_n)(code, buf.as_mut_ptr(), buf.len());
        CStr::from_ptr(buf.as_ptr()).to_string_lossy().into_owned()
    }
}

#[derive(Clone, Debug)]
pub struct ParsedHttpsUrl {
    pub host: String,
    pub port: u16,
    pub netloc: String,
    pub path_query: String,
}

pub fn require_https(url: &str) -> Result<ParsedHttpsUrl, String> {
    if !url.starts_with("https://") || url.contains('#') {
        return Err("An HTTPS URL without credentials or fragment is required".into());
    }
    let rest = &url["https://".len()..];
    let (auth, path_query) = match rest.find('/') {
        Some(idx) => (&rest[..idx], &rest[idx..]),
        None => (rest, "/"),
    };
    if auth.is_empty() || auth.contains('@') {
        return Err("An HTTPS URL without credentials or fragment is required".into());
    }
    let (host, port) = if auth.starts_with('[') {
        let end = auth.find(']').ok_or("Invalid IPv6 host in HTTPS URL")?;
        let h = &auth[1..end];
        let after = &auth[end + 1..];
        let p = if after.is_empty() {
            443
        } else if let Some(ps) = after.strip_prefix(':') {
            ps.parse::<u16>().map_err(|_| "Invalid port in HTTPS URL")?
        } else {
            return Err("Invalid host authority in HTTPS URL".into());
        };
        (h.to_string(), p)
    } else if let Some((h, ps)) = auth.rsplit_once(':') {
        let p = ps.parse::<u16>().map_err(|_| "Invalid port in HTTPS URL")?;
        (h.to_string(), p)
    } else {
        (auth.to_string(), 443)
    };
    if host.is_empty() || host.contains(char::is_whitespace) {
        return Err("An HTTPS URL without credentials or fragment is required".into());
    }
    Ok(ParsedHttpsUrl {
        host,
        port,
        netloc: auth.to_string(),
        path_query: path_query.to_string(),
    })
}

pub fn migrate_legacy_url(raw: &str) -> String {
    let trimmed = raw.trim();
    for (prefix, repl) in [("ws://", "https://"), ("wss://", "https://"), ("http://", "https://")] {
        if let Some(rest) = trimmed.strip_prefix(prefix) {
            let (auth, pq) = match rest.find('/') {
                Some(idx) => (&rest[..idx], &rest[idx..]),
                None => (rest, ""),
            };
            let clean_pq = if pq.trim_end_matches('/') == "/voice" {
                ""
            } else {
                pq.trim_end_matches('/')
            };
            return format!("{repl}{auth}{clean_pq}");
        }
    }
    trimmed.trim_end_matches('/').to_string()
}

struct SslGuard {
    fns: &'static OpenSslFns,
    ctx: *mut c_void,
    ssl: *mut c_void,
}

impl Drop for SslGuard {
    fn drop(&mut self) {
        unsafe {
            if !self.ssl.is_null() {
                (self.fns.SSL_shutdown)(self.ssl);
                (self.fns.SSL_free)(self.ssl);
            }
            if !self.ctx.is_null() {
                (self.fns.SSL_CTX_free)(self.ctx);
            }
        }
    }
}

fn wait_fd(fd: c_int, events: i16, deadline: Instant, cancel: Option<&dyn Fn() -> bool>) -> Result<(), String> {
    if let Some(c) = cancel {
        if c() {
            return Err("Request cancelled".into());
        }
    }
    let now = Instant::now();
    if now >= deadline {
        return Err("TimeoutError: HTTPS deadline exceeded".into());
    }
    let rem_ms = (deadline - now).as_millis().min(100) as c_int;
    let mut pfd = PollFd { fd, events, revents: 0 };
    unsafe {
        poll(&mut pfd, 1, rem_ms.max(1));
    }
    Ok(())
}

fn tls_connect(
    parsed: &ParsedHttpsUrl,
    ca_file: Option<&str>,
    deadline: Instant,
    cancel: Option<&dyn Fn() -> bool>,
) -> Result<(TcpStream, SslGuard), String> {
    let fns = load_openssl()?;
    let addr_str = if parsed.host.contains(':') {
        format!("[{}]:{}", parsed.host, parsed.port)
    } else {
        format!("{}:{}", parsed.host, parsed.port)
    };
    let addrs: Vec<_> = addr_str
        .to_socket_addrs()
        .map_err(|e| format!("DNS resolve failed for {addr_str}: {e}"))?
        .collect();
    if addrs.is_empty() {
        return Err(format!("No address resolved for {addr_str}"));
    }
    let mut last_err = String::from("TCP connect failed");
    let mut tcp = None;
    for addr in addrs {
        if Instant::now() >= deadline {
            return Err("TimeoutError: HTTPS connection deadline exceeded".into());
        }
        let rem = deadline - Instant::now();
        match TcpStream::connect_timeout(&addr, rem.min(Duration::from_secs(10))) {
            Ok(s) => {
                tcp = Some(s);
                break;
            }
            Err(e) => last_err = format!("Connect {addr}: {e}"),
        }
    }
    let tcp = tcp.ok_or(last_err)?;
    let _ = tcp.set_nodelay(true);
    tcp.set_nonblocking(true).map_err(|e| e.to_string())?;
    let fd = tcp.as_raw_fd();

    let ctx = unsafe { (fns.SSL_CTX_new)((fns.TLS_client_method)()) };
    if ctx.is_null() {
        return Err("SSL_CTX_new failed".into());
    }
    let mut guard = SslGuard { fns, ctx, ssl: std::ptr::null_mut() };
    unsafe {
        (fns.SSL_CTX_set_verify)(ctx, SSL_VERIFY_PEER, std::ptr::null());
        if let Some(ca) = ca_file.filter(|s| !s.is_empty()) {
            let c_ca = CString::new(ca).map_err(|_| "Invalid CA path")?;
            if (fns.SSL_CTX_load_verify_locations)(ctx, c_ca.as_ptr(), std::ptr::null()) != 1 {
                return Err(format!("TLS error loading CA {ca}: {}", ossl_err(fns)));
            }
        } else if (fns.SSL_CTX_set_default_verify_paths)(ctx) != 1 {
            let fallback = CString::new("/etc/ssl/certs/ca-certificates.crt").unwrap();
            let _ = (fns.SSL_CTX_load_verify_locations)(ctx, fallback.as_ptr(), std::ptr::null());
        }
        let ssl = (fns.SSL_new)(ctx);
        if ssl.is_null() {
            return Err("SSL_new failed".into());
        }
        guard.ssl = ssl;
        if (fns.SSL_set_fd)(ssl, fd) != 1 {
            return Err("SSL_set_fd failed".into());
        }
        let c_host = CString::new(parsed.host.as_str()).map_err(|_| "Invalid host")?;
        let param = (fns.SSL_get0_param)(ssl);
        if parsed.host.parse::<IpAddr>().is_ok() {
            if (fns.X509_VERIFY_PARAM_set1_ip_asc)(param, c_host.as_ptr()) != 1 {
                return Err("Failed setting TLS IP verify param".into());
            }
        } else {
            (fns.SSL_ctrl)(
                ssl,
                SSL_CTRL_SET_TLSEXT_HOSTNAME,
                TLSEXT_NAMETYPE_HOST_NAME,
                c_host.as_ptr() as *mut c_void,
            );
            if (fns.X509_VERIFY_PARAM_set1_host)(param, c_host.as_ptr(), parsed.host.len()) != 1 {
                return Err("Failed setting TLS host verify param".into());
            }
        }
        loop {
            if let Some(c) = cancel {
                if c() {
                    return Err("Request cancelled".into());
                }
            }
            if Instant::now() >= deadline {
                return Err("TimeoutError: TLS handshake deadline exceeded".into());
            }
            let r = (fns.SSL_connect)(ssl);
            if r == 1 {
                break;
            }
            let err = (fns.SSL_get_error)(ssl, r);
            if err == SSL_ERROR_WANT_READ {
                wait_fd(fd, POLLIN, deadline, cancel)?;
            } else if err == SSL_ERROR_WANT_WRITE {
                wait_fd(fd, POLLOUT, deadline, cancel)?;
            } else {
                let vr = (fns.SSL_get_verify_result)(ssl);
                if vr != 0 {
                    return Err(format!("TLS certificate verification failed (code {vr})"));
                }
                return Err(format!("TLS handshake error: {}", ossl_err(fns)));
            }
        }
    }
    Ok((tcp, guard))
}

fn ssl_write_all(
    guard: &SslGuard,
    fd: c_int,
    data: &[u8],
    deadline: Instant,
    cancel: Option<&dyn Fn() -> bool>,
) -> Result<(), String> {
    let mut off = 0usize;
    while off < data.len() {
        if let Some(c) = cancel {
            if c() {
                return Err("Request cancelled".into());
            }
        }
        if Instant::now() >= deadline {
            return Err("TimeoutError: HTTPS write deadline exceeded".into());
        }
        let n = unsafe {
            let chunk = (data.len() - off).min(65536) as c_int;
            (guard.fns.SSL_write)(guard.ssl, data[off..].as_ptr() as *const c_void, chunk)
        };
        if n > 0 {
            off += n as usize;
            continue;
        }
        let err = unsafe { (guard.fns.SSL_get_error)(guard.ssl, n) };
        match err {
            SSL_ERROR_WANT_READ => wait_fd(fd, POLLIN, deadline, cancel)?,
            SSL_ERROR_WANT_WRITE => wait_fd(fd, POLLOUT, deadline, cancel)?,
            _ => return Err(format!("TLS write failed: {}", ossl_err(guard.fns))),
        }
    }
    Ok(())
}

fn ssl_read_some(
    guard: &SslGuard,
    fd: c_int,
    buf: &mut [u8],
    deadline: Instant,
    cancel: Option<&dyn Fn() -> bool>,
) -> Result<usize, String> {
    loop {
        if let Some(c) = cancel {
            if c() {
                return Err("Request cancelled".into());
            }
        }
        if Instant::now() >= deadline {
            return Err("TimeoutError: HTTPS response deadline exceeded".into());
        }
        let n = unsafe {
            (guard.fns.SSL_read)(
                guard.ssl,
                buf.as_mut_ptr() as *mut c_void,
                buf.len().min(65536) as c_int,
            )
        };
        if n > 0 {
            return Ok(n as usize);
        }
        let err = unsafe { (guard.fns.SSL_get_error)(guard.ssl, n) };
        match err {
            SSL_ERROR_ZERO_RETURN => return Ok(0),
            SSL_ERROR_WANT_READ => wait_fd(fd, POLLIN, deadline, cancel)?,
            SSL_ERROR_WANT_WRITE => wait_fd(fd, POLLOUT, deadline, cancel)?,
            _ => {
                let msg = ossl_err(guard.fns);
                if n == 0 && msg == "TLS error" {
                    return Ok(0);
                }
                return Err(format!("TLS read failed: {msg}"));
            }
        }
    }
}

fn find_header_end(buf: &[u8]) -> Option<usize> {
    buf.windows(4).position(|w| w == b"\r\n\r\n").map(|p| p + 4)
}

fn parse_headers(raw: &[u8]) -> Result<(u16, BTreeMap<String, String>), String> {
    let text = String::from_utf8_lossy(raw);
    let mut lines = text.split("\r\n");
    let status_line = lines.next().ok_or("Empty HTTP response")?;
    let mut parts = status_line.split_whitespace();
    let _proto = parts.next().ok_or("Malformed HTTP status line")?;
    let code: u16 = parts
        .next()
        .and_then(|s| s.parse().ok())
        .ok_or_else(|| format!("Invalid HTTP status line: {status_line}"))?;
    let mut hdrs = BTreeMap::new();
    for line in lines {
        if line.is_empty() {
            break;
        }
        if let Some((k, v)) = line.split_once(':') {
            hdrs.insert(k.trim().to_ascii_lowercase(), v.trim().to_string());
        }
    }
    Ok((code, hdrs))
}

fn decode_chunked(
    pending: &mut Vec<u8>,
    guard: &SslGuard,
    fd: c_int,
    deadline: Instant,
    cancel: Option<&dyn Fn() -> bool>,
    limit: usize,
    on_chunk: &mut dyn FnMut(&[u8]) -> Result<(), String>,
) -> Result<usize, String> {
    let mut total = 0usize;
    let mut tmp = vec![0u8; 32768];
    loop {
        while !pending.windows(2).any(|w| w == b"\r\n") {
            let n = ssl_read_some(guard, fd, &mut tmp, deadline, cancel)?;
            if n == 0 {
                return Err("Truncated chunked HTTPS response".into());
            }
            pending.extend_from_slice(&tmp[..n]);
            if pending.len() > 65536 {
                return Err("Oversized chunk header".into());
            }
        }
        let crlf = pending.windows(2).position(|w| w == b"\r\n").unwrap();
        let line = String::from_utf8_lossy(&pending[..crlf]).to_string();
        let size_str = line.split(';').next().unwrap_or("").trim();
        let sz = usize::from_str_radix(size_str, 16)
            .map_err(|_| format!("Invalid chunk size: {line}"))?;
        pending.drain(..crlf + 2);
        if sz == 0 {
            return Ok(total);
        }
        total = total.checked_add(sz).ok_or("Response size overflow")?;
        if total > limit {
            return Err("HTTPS response exceeds size limit".into());
        }
        let mut need = sz + 2;
        while need > 0 {
            if !pending.is_empty() {
                let take = pending.len().min(need);
                let data_take = take.min(need.saturating_sub(2));
                if data_take > 0 {
                    on_chunk(&pending[..data_take])?;
                }
                pending.drain(..take);
                need -= take;
                continue;
            }
            let n = ssl_read_some(guard, fd, &mut tmp, deadline, cancel)?;
            if n == 0 {
                return Err("Truncated chunked HTTPS body".into());
            }
            pending.extend_from_slice(&tmp[..n]);
        }
    }
}

pub fn https_stream(
    url: &str,
    method: &str,
    body: Option<&[u8]>,
    headers: &[(&str, &str)],
    ca_file: Option<&str>,
    timeout_secs: u64,
    limit: usize,
    cancel: Option<&dyn Fn() -> bool>,
    mut on_chunk: impl FnMut(&[u8]) -> Result<(), String>,
) -> Result<(), String> {
    let deadline = Instant::now() + Duration::from_secs(timeout_secs.max(1));
    let mut cur_url = url.to_string();
    let mut cur_method = method.to_string();
    let mut cur_body = body;
    let mut hdr_map: Vec<(String, String)> = headers
        .iter()
        .map(|(k, v)| (k.to_string(), v.to_string()))
        .collect();

    for _hop in 0..=5 {
        let parsed = require_https(&cur_url)?;
        let (tcp, guard) = tls_connect(&parsed, ca_file, deadline, cancel)?;
        let fd = tcp.as_raw_fd();

        let mut req = format!(
            "{} {} HTTP/1.1\r\nHost: {}\r\nUser-Agent: AtriOS/1\r\nConnection: close\r\n",
            cur_method, parsed.path_query, parsed.netloc
        );
        for (k, v) in &hdr_map {
            req.push_str(&format!("{k}: {v}\r\n"));
        }
        if let Some(b) = cur_body {
            req.push_str(&format!("Content-Length: {}\r\n", b.len()));
        }
        req.push_str("\r\n");

        if let Some(b) = cur_body {
            if b.len() <= 262_144 {
                let mut full = Vec::with_capacity(req.len() + b.len());
                full.extend_from_slice(req.as_bytes());
                full.extend_from_slice(b);
                ssl_write_all(&guard, fd, &full, deadline, cancel)?;
            } else {
                ssl_write_all(&guard, fd, req.as_bytes(), deadline, cancel)?;
                ssl_write_all(&guard, fd, b, deadline, cancel)?;
            }
        } else {
            ssl_write_all(&guard, fd, req.as_bytes(), deadline, cancel)?;
        }

        let mut head_buf = Vec::with_capacity(4096);
        let mut tmp = vec![0u8; 16384];
        let hdr_end = loop {
            if let Some(pos) = find_header_end(&head_buf) {
                break pos;
            }
            if head_buf.len() > 65536 {
                return Err("HTTPS response headers too large".into());
            }
            let n = ssl_read_some(&guard, fd, &mut tmp, deadline, cancel)?;
            if n == 0 {
                return Err("EOF before end of HTTPS headers".into());
            }
            head_buf.extend_from_slice(&tmp[..n]);
        };

        let (code, resp_hdrs) = parse_headers(&head_buf[..hdr_end])?;
        let mut pending = head_buf.split_off(hdr_end);

        if matches!(code, 301 | 302 | 303 | 307 | 308) {
            let loc = resp_hdrs
                .get("location")
                .ok_or("Redirect without Location header")?
                .clone();
            let next_url = if loc.starts_with('/') {
                format!("https://{}{loc}", parsed.netloc)
            } else {
                loc
            };
            let next_parsed = require_https(&next_url)?;
            if next_parsed.netloc != parsed.netloc {
                hdr_map.retain(|(k, _)| !k.eq_ignore_ascii_case("Authorization"));
            }
            if matches!(code, 301 | 302 | 303) {
                cur_method = "GET".into();
                cur_body = None;
            }
            cur_url = next_url;
            continue;
        }

        if !(200..300).contains(&code) {
            return Err(format!("HTTP Error {code}"));
        }

        let declared = match resp_hdrs.get("content-length") {
            Some(s) => {
                let n = s
                    .parse::<usize>()
                    .map_err(|_| "Invalid Content-Length header")?;
                if n > limit {
                    return Err("HTTPS response exceeds size limit".into());
                }
                Some(n)
            }
            None => None,
        };

        let is_chunked = resp_hdrs
            .get("transfer-encoding")
            .map(|s| s.to_ascii_lowercase().contains("chunked"))
            .unwrap_or(false);

        if is_chunked {
            decode_chunked(&mut pending, &guard, fd, deadline, cancel, limit, &mut on_chunk)?;
            return Ok(());
        }

        let mut total = 0usize;
        if !pending.is_empty() {
            total += pending.len();
            if total > limit {
                return Err("HTTPS response exceeds size limit".into());
            }
            on_chunk(&pending)?;
            pending.clear();
        }
        loop {
            if let Some(exp) = declared {
                if total >= exp {
                    break;
                }
            }
            let n = ssl_read_some(&guard, fd, &mut tmp, deadline, cancel)?;
            if n == 0 {
                break;
            }
            total = total.checked_add(n).ok_or("Response overflow")?;
            if total > limit {
                return Err("HTTPS response exceeds size limit".into());
            }
            on_chunk(&tmp[..n])?;
        }
        if let Some(exp) = declared {
            if total != exp {
                return Err("Truncated HTTPS response".into());
            }
        }
        return Ok(());
    }
    Err("Too many HTTPS redirects".into())
}

pub fn request_bytes(
    url: &str,
    data: Option<&[u8]>,
    headers: &[(&str, &str)],
    ca_file: Option<&str>,
    timeout_secs: u64,
    limit: usize,
    cancel: Option<&dyn Fn() -> bool>,
) -> Result<Vec<u8>, String> {
    let mut out = Vec::new();
    let method = if data.is_some() { "POST" } else { "GET" };
    https_stream(url, method, data, headers, ca_file, timeout_secs, limit, cancel, |chunk| {
        out.extend_from_slice(chunk);
        Ok(())
    })?;
    Ok(out)
}

pub fn request_json(
    url: &str,
    payload: Option<&Json>,
    headers: &[(&str, &str)],
    ca_file: Option<&str>,
    timeout_secs: u64,
    limit: usize,
    cancel: Option<&dyn Fn() -> bool>,
) -> Result<Json, String> {
    let body_str = payload.map(|j| j.to_string_compact());
    let mut hdrs: Vec<(&str, &str)> = Vec::with_capacity(headers.len() + 2);
    hdrs.extend_from_slice(headers);
    hdrs.push(("Accept", "application/json"));
    if body_str.is_some() {
        hdrs.push(("Content-Type", "application/json"));
    }
    let bytes = request_bytes(
        url,
        body_str.as_ref().map(|s| s.as_bytes()),
        &hdrs,
        ca_file,
        timeout_secs,
        limit,
        cancel,
    )?;
    let text = String::from_utf8(bytes).map_err(|_| "Invalid UTF-8 in JSON response")?;
    let val = Json::parse(&text)?;
    if val.as_obj().is_none() {
        return Err("Expected a JSON object".into());
    }
    Ok(val)
}
