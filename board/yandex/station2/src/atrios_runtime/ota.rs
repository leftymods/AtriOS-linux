use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::raw::c_int;
use std::os::unix::fs::PermissionsExt;
use std::os::unix::io::AsRawFd;
use std::path::{Component, Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::{Duration, Instant};

use crate::https::{https_stream, request_bytes, request_json, require_https};
use crate::json::{obj, Json};
use crate::status::{set_error, wlan_is_connected, OTA_STATUS_FILE};
use crate::sys::{
    atomic_write_bytes, flock, free_bytes, hex_lower, now_unix_i64, reboot, sha256_file_hex,
    signal, sync, sync_dir, uname_strings, Sha256, LOCK_EX, LOCK_NB, RB_AUTOBOOT, SIGINT, SIGTERM,
};

extern "C" {
    fn nice(inc: c_int) -> c_int;
}

const CONFIG_FILE: &str = "/etc/atrios/updates.json";
const STATE_FILE: &str = "/var/lib/atrios/ota_status.json";
pub const RUNTIME_STATUS_FILE: &str = OTA_STATUS_FILE;
const JOURNAL: &str = "/var/lib/atrios/ota-transaction.json";
const BACKUP_DIR: &str = "/var/lib/atrios/ota-rollback";
const CACHE_DIR: &str = "/var/cache/atrios/updates";
const VERSION_FILE: &str = "/etc/atrios-version";
const BOOT_DIR: &str = "/boot";
const ROOT_DIR: &str = "/";
const LOCK_FILE: &str = "/var/run/atrios/ota.lock";

const MAX_DOWNLOAD: u64 = 512 * 1024 * 1024;
const MAX_EXPANDED: u64 = 2 * 1024 * 1024 * 1024;
const MAX_MEMBERS: usize = 50_000;
const INITIAL_BOOT_DELAY_SECS: u64 = 90;
const CHECK_TIMEOUT_SECS: u64 = 15;

static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

extern "C" fn on_stop(_sig: c_int) {
    STOP_REQUESTED.store(true, Ordering::SeqCst);
}

#[derive(Clone)]
struct OtaConfig {
    repository: String,
    enabled: bool,
    interval_seconds: u64,
    public_key: String,
}

fn load_config() -> Result<OtaConfig, String> {
    let mut cfg = OtaConfig {
        repository: "leftymods/AtriOS-linux".into(),
        enabled: true,
        interval_seconds: 21600,
        public_key: "/etc/atrios/ota-public.pem".into(),
    };
    if Path::new(CONFIG_FILE).exists() {
        let txt = fs::read_to_string(CONFIG_FILE).map_err(|e| e.to_string())?;
        let j = Json::parse(&txt)?;
        let Some(m) = j.as_obj() else { return Err("Invalid updates.json".into()); };
        if let Some(v) = m.get("repository").and_then(|x| x.as_str()) { cfg.repository = v.into(); }
        if let Some(v) = m.get("enabled").and_then(|x| x.as_bool()) { cfg.enabled = v; }
        if let Some(v) = m.get("interval_seconds").and_then(|x| x.as_i64()) { cfg.interval_seconds = v.max(60) as u64; }
        if let Some(v) = m.get("public_key").and_then(|x| x.as_str()) { cfg.public_key = v.into(); }
    }
    if let Some((owner, repo)) = cfg.repository.split_once('/') {
        let valid = |s: &str| !s.is_empty() && s.bytes().all(|b| b.is_ascii_alphanumeric() || matches!(b, b'_' | b'.' | b'-'));
        if !valid(owner) || !valid(repo) {
            return Err("Invalid GitHub repository".into());
        }
    } else {
        return Err("Invalid GitHub repository".into());
    }
    Ok(cfg)
}

fn device_id() -> Result<String, String> {
    let raw = if Path::new("/etc/atrios-board").exists() {
        fs::read_to_string("/etc/atrios-board").unwrap_or_default().trim().to_string()
    } else if Path::new("/etc/atrios-device").exists() {
        let mut val = String::new();
        for line in fs::read_to_string("/etc/atrios-device").unwrap_or_default().lines() {
            if let Some(r) = line.strip_prefix("DEVICE_CODENAME=") {
                val = r.trim().trim_matches('"').trim_matches('\'').to_string();
            }
        }
        val
    } else {
        String::new()
    };
    let ok = !raw.is_empty()
        && raw.len() <= 64
        && raw.as_bytes()[0].is_ascii_lowercase() | raw.as_bytes()[0].is_ascii_digit()
        && raw.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-');
    if !ok {
        return Err("Unknown device: configure /etc/atrios-board before updating".into());
    }
    Ok(raw)
}

fn current_version() -> String {
    fs::read_to_string(VERSION_FILE)
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "1.0".into())
}

fn version_key(val: &str) -> Result<[u64; 4], String> {
    let s = val.strip_prefix('v').unwrap_or(val);
    let parts: Vec<&str> = s.split('.').collect();
    if parts.is_empty() || parts.len() > 4 || parts.iter().any(|p| p.is_empty() || !p.bytes().all(|b| b.is_ascii_digit())) {
        return Err("Stable OTA versions must contain 1-4 numeric components".into());
    }
    let mut key = [0u64; 4];
    for (i, p) in parts.iter().enumerate() {
        key[i] = p.parse::<u64>().map_err(|_| "Invalid version component")?;
    }
    Ok(key)
}

fn valid_tag_str(s: &str, max_len: usize) -> bool {
    !s.is_empty()
        && s.len() <= max_len
        && s.bytes().all(|b| b.is_ascii_alphanumeric() || matches!(b, b'.' | b'_' | b'+' | b'-'))
}

fn safe_rel(name: &str) -> Result<String, String> {
    if name.is_empty() || name.contains('\\') || name.contains('\0') || name.starts_with('/') {
        return Err("Invalid archive path".into());
    }
    let mut parts = Vec::new();
    for comp in Path::new(name).components() {
        match comp {
            Component::Normal(p) => {
                let ps = p.to_str().ok_or("Invalid UTF-8 path")?;
                if !ps.is_empty() && ps != "." {
                    parts.push(ps);
                }
            }
            Component::CurDir => {}
            _ => return Err("Archive path escapes extraction directory".into()),
        }
    }
    if parts.is_empty() {
        return Err("Archive path escapes extraction directory".into());
    }
    Ok(parts.join("/"))
}

// --- Bounded streaming USTAR/PAX tar extractor ---
struct BoundedReader<R: Read> {
    inner: R,
    total: u64,
}

impl<R: Read> BoundedReader<R> {
    fn read_exact_bounded(&mut self, buf: &mut [u8]) -> Result<bool, String> {
        let mut off = 0;
        while off < buf.len() {
            let n = self.inner.read(&mut buf[off..]).map_err(|e| e.to_string())?;
            if n == 0 {
                if off == 0 {
                    return Ok(false);
                }
                return Err("Unexpected EOF in tar stream".into());
            }
            self.total = self.total.saturating_add(n as u64);
            if self.total > MAX_EXPANDED {
                return Err("Expanded tar stream exceeds limit".into());
            }
            off += n;
        }
        Ok(true)
    }

    fn discard_exact(&mut self, mut len: u64) -> Result<(), String> {
        let mut scratch = [0u8; 8192];
        while len > 0 {
            let want = (len as usize).min(scratch.len());
            if !self.read_exact_bounded(&mut scratch[..want])? {
                return Err("Unexpected EOF skipping tar padding".into());
            }
            len -= want as u64;
        }
        Ok(())
    }
}

fn parse_octal_or_base256(field: &[u8]) -> Result<u64, String> {
    if field[0] & 0x80 != 0 {
        let mut val = 0u64;
        for &b in &field[1..] {
            val = (val << 8) | (b as u64);
        }
        return Ok(val);
    }
    let s = String::from_utf8_lossy(field);
    let trimmed = s.trim_matches(|c: char| c == '\0' || c == ' ');
    if trimmed.is_empty() {
        return Ok(0);
    }
    u64::from_str_radix(trimmed, 8).map_err(|_| format!("Invalid octal field in tar: {trimmed}"))
}

fn cstr_field(b: &[u8]) -> String {
    let len = b.iter().position(|&x| x == 0).unwrap_or(b.len());
    String::from_utf8_lossy(&b[..len]).into_owned()
}

fn parse_pax_path(data: &[u8]) -> Option<String> {
    let txt = String::from_utf8_lossy(data);
    for line in txt.lines() {
        if let Some((_len, rest)) = line.split_once(' ') {
            if let Some(p) = rest.strip_prefix("path=") {
                return Some(p.to_string());
            }
        }
    }
    None
}

fn extract_tar_stream<R: Read>(reader: R, dest: &Path) -> Result<u64, String> {
    let mut br = BoundedReader { inner: reader, total: 0 };
    let mut hdr = [0u8; 512];
    let mut count = 0usize;
    let mut total_files_bytes = 0u64;
    let mut seen: BTreeSet<String> = BTreeSet::new();
    let mut next_pax_path: Option<String> = None;
    let mut next_gnu_longname: Option<String> = None;

    while br.read_exact_bounded(&mut hdr)? {
        if hdr.iter().all(|&b| b == 0) {
            break;
        }
        let stored_sum = parse_octal_or_base256(&hdr[148..156])?;
        let mut calc_sum: u64 = 8 * (b' ' as u64);
        for (i, &b) in hdr.iter().enumerate() {
            if !(148..156).contains(&i) {
                calc_sum += b as u64;
            }
        }
        if stored_sum != calc_sum {
            return Err("Corrupt tar header checksum".into());
        }
        let typeflag = hdr[156];
        let size = parse_octal_or_base256(&hdr[124..136])?;
        let mode = (parse_octal_or_base256(&hdr[100..108])? & 0o777) as u32;
        let pad = (512 - (size % 512)) % 512;

        if matches!(typeflag, b'x' | b'X' | b'g' | b'L' | b'K') {
            if size > 65536 {
                return Err("Oversized tar metadata".into());
            }
            let mut meta = vec![0u8; size as usize];
            if !br.read_exact_bounded(&mut meta)? {
                return Err("Truncated tar metadata".into());
            }
            br.discard_exact(pad)?;
            if typeflag == b'x' {
                next_pax_path = parse_pax_path(&meta);
            } else if typeflag == b'L' {
                next_gnu_longname = Some(cstr_field(&meta));
            }
            continue;
        }

        count += 1;
        if count > MAX_MEMBERS {
            return Err("Too many archive members".into());
        }

        let raw_name = if let Some(p) = next_pax_path.take() {
            next_gnu_longname = None;
            p
        } else if let Some(p) = next_gnu_longname.take() {
            p
        } else {
            let prefix = cstr_field(&hdr[345..500]);
            let name = cstr_field(&hdr[0..100]);
            if prefix.is_empty() { name } else { format!("{prefix}/{name}") }
        };

        let is_dir = typeflag == b'5';
        let is_file = typeflag == b'0' || typeflag == 0;
        if !is_dir && !is_file {
            return Err("Duplicate or unsupported archive member".into());
        }
        if is_dir && size != 0 {
            return Err("Directory with payload is not supported".into());
        }
        let rel = safe_rel(&raw_name)?;
        if !seen.insert(rel.clone()) {
            return Err("Duplicate or unsupported archive member".into());
        }

        let target = dest.join(&rel);
        if is_dir {
            fs::create_dir_all(&target).map_err(|e| e.to_string())?;
            br.discard_exact(pad)?;
            continue;
        }

        total_files_bytes = total_files_bytes.saturating_add(size);
        if total_files_bytes > MAX_EXPANDED {
            return Err("Expanded OTA size exceeds limit".into());
        }
        if let Some(p) = target.parent() {
            fs::create_dir_all(p).map_err(|e| e.to_string())?;
        }
        let mut out = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&target)
            .map_err(|e| e.to_string())?;
        let mut rem = size;
        let mut buf = [0u8; 65536];
        while rem > 0 {
            let take = (rem as usize).min(buf.len());
            if !br.read_exact_bounded(&mut buf[..take])? {
                return Err("Truncated archive member payload".into());
            }
            out.write_all(&buf[..take]).map_err(|e| e.to_string())?;
            rem -= take as u64;
        }
        out.set_permissions(fs::Permissions::from_mode(mode)).map_err(|e| e.to_string())?;
        br.discard_exact(pad)?;
    }
    Ok(total_files_bytes)
}

fn extract_safe(archive: &Path, dest: &Path) -> Result<u64, String> {
    let mut f = File::open(archive).map_err(|e| e.to_string())?;
    let mut magic = [0u8; 6];
    let n = f.read(&mut magic).map_err(|e| e.to_string())?;
    let m = &magic[..n];
    let decomp = if m.starts_with(&[0x1f, 0x8b]) {
        Some("gzip")
    } else if m.starts_with(b"\xfd7zXZ\0") {
        Some("xz")
    } else if m.starts_with(b"BZh") {
        Some("bzip2")
    } else {
        None
    };

    if let Some(prog) = decomp {
        drop(f);
        let mut child: Child = Command::new(prog)
            .args(["-dc", &archive.to_string_lossy()])
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|e| format!("{prog} spawn failed: {e}"))?;
        let stdout = child.stdout.take().ok_or("Missing decompressor stdout")?;
        let res = extract_tar_stream(stdout, dest);
        let st = child.wait().map_err(|e| e.to_string())?;
        res?;
        if !st.success() {
            return Err(format!("{prog} decompression failed"));
        }
        Ok(0)
    } else {
        f.seek(SeekFrom::Start(0)).map_err(|e| e.to_string())?;
        extract_tar_stream(f, dest)
    }
}

struct LockGuard {
    _file: File,
}

fn acquire_update_lock() -> Result<LockGuard, String> {
    if let Some(p) = Path::new(LOCK_FILE).parent() {
        fs::create_dir_all(p).map_err(|e| e.to_string())?;
    }
    let file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(LOCK_FILE)
        .map_err(|e| e.to_string())?;
    let rc = unsafe { flock(file.as_raw_fd(), LOCK_EX | LOCK_NB) };
    if rc != 0 {
        return Err("Another update operation is running".into());
    }
    Ok(LockGuard { _file: file })
}

fn atomic_json(path: &Path, val: &Json) -> Result<(), String> {
    let mut s = val.to_string_pretty();
    s.push('\n');
    atomic_write_bytes(path, s.as_bytes(), 0o644, true).map_err(|e| e.to_string())
}

fn atomic_copy(src: &Path, dst: &Path) -> Result<(), String> {
    let data = fs::read(src).map_err(|e| e.to_string())?;
    let mode = fs::metadata(src).map(|m| m.permissions().mode() & 0o777).unwrap_or(0o644);
    atomic_write_bytes(dst, &data, mode, true).map_err(|e| e.to_string())
}

fn verify_bundle(work: &Path, cfg: &OtaConfig) -> Result<(Json, BTreeMap<String, PathBuf>), String> {
    let manifest_path = work.join("manifest.json");
    let sig_path = work.join("manifest.sig");
    let key_path = Path::new(&cfg.public_key);
    if !key_path.is_file() {
        return Err(format!("OTA trust key not provisioned: {}", cfg.public_key));
    }
    let meta = fs::metadata(&manifest_path).map_err(|_| "Missing or oversized signed manifest".to_string())?;
    if !meta.is_file() || meta.len() > 1024 * 1024 || !sig_path.is_file() {
        return Err("Missing or oversized signed manifest".into());
    }
    let out = Command::new("openssl")
        .args([
            "dgst", "-sha256", "-verify", &cfg.public_key, "-signature",
            &sig_path.to_string_lossy(), &manifest_path.to_string_lossy(),
        ])
        .output()
        .map_err(|e| e.to_string())?;
    if !out.status.success() {
        return Err("Invalid OTA manifest signature".into());
    }
    let manifest = Json::parse(&fs::read_to_string(&manifest_path).map_err(|e| e.to_string())?)?;
    if manifest.get("schema").and_then(|v| v.as_i64()) != Some(1)
        || manifest.get_str("device_id") != Some(&device_id()?)
    {
        return Err("Unsupported OTA schema or wrong target device".into());
    }
    let ver = manifest.get_str("version").ok_or("Invalid OTA version")?;
    if !valid_tag_str(ver, 64) {
        return Err("Invalid OTA version".into());
    }
    if manifest.get("debs").is_some_and(|v| !matches!(v, Json::Null)) {
        return Err("Only signed kernel, DTB and regular-file rootfs overlays are supported".into());
    }
    let files = manifest.get("files").and_then(|v| v.as_obj()).ok_or("Missing files in manifest")?;
    if files.is_empty() || files.keys().any(|k| !matches!(k.as_str(), "kernel" | "dtb" | "rootfs")) {
        return Err("Unsupported OTA files in manifest".into());
    }
    let mut payloads = BTreeMap::new();
    for (kind, entry) in files {
        let sha = entry.get_str("sha256").ok_or_else(|| format!("Mandatory SHA-256 missing for {kind}"))?;
        if sha.len() != 64 || !sha.bytes().all(|b| b.is_ascii_hexdigit()) {
            return Err(format!("Mandatory SHA-256 missing for {kind}"));
        }
        let rel = safe_rel(entry.get_str("path").ok_or("Missing payload path")?)?;
        let p = work.join(&rel);
        if rel == "manifest.json" || rel == "manifest.sig" || !p.is_file() {
            return Err(format!("Missing payload or SHA-256 mismatch for {kind}"));
        }
        let got = sha256_file_hex(&p).map_err(|e| e.to_string())?;
        if !got.eq_ignore_ascii_case(sha) {
            return Err(format!("Missing payload or SHA-256 mismatch for {kind}"));
        }
        payloads.insert(kind.clone(), p);
    }
    Ok((manifest, payloads))
}

fn validate_boot_payloads(payloads: &BTreeMap<String, PathBuf>) -> Result<(), String> {
    if let Some(k) = payloads.get("kernel") {
        let data = fs::read(k).map_err(|e| e.to_string())?;
        if data.len() < 64 || data.len() > 128 * 1024 * 1024 || &data[56..60] != b"ARM\x64" {
            return Err("Invalid ARM64 kernel Image header or size".into());
        }
        let sz = u64::from_le_bytes(data[16..24].try_into().unwrap());
        if !(64..=128 * 1024 * 1024).contains(&sz) {
            return Err("Invalid ARM64 kernel Image header or size".into());
        }
    }
    if let Some(d) = payloads.get("dtb") {
        let data = fs::read(d).map_err(|e| e.to_string())?;
        if data.len() < 40 || data[0..4] != [0xd0, 0x0d, 0xfe, 0xed] {
            return Err("Invalid DTB header or size".into());
        }
        let sz = u32::from_be_bytes(data[4..8].try_into().unwrap()) as usize;
        if sz < 40 || sz > data.len() {
            return Err("Invalid DTB header or size".into());
        }
    }
    Ok(())
}

pub fn ensure_boot_mounted() -> Result<(), String> {
    let inspect = || -> Option<String> {
        let o = Command::new("findmnt")
            .args(["-n", "-o", "FSTYPE", "--mountpoint", BOOT_DIR])
            .output()
            .ok()?;
        if o.status.success() {
            Some(String::from_utf8_lossy(&o.stdout).trim().to_string())
        } else {
            None
        }
    };
    let fstype = match inspect() {
        Some(f) => f,
        None => {
            let st = Command::new("mount").arg(BOOT_DIR).status().map_err(|e| e.to_string())?;
            if !st.success() {
                return Err("Failed mounting /boot".into());
            }
            inspect().unwrap_or_default()
        }
    };
    if fstype != "vfat" {
        return Err("Refusing OTA: /boot must be the mounted FAT boot partition".into());
    }
    let src_of = |mp: &str| -> Result<PathBuf, String> {
        let o = Command::new("findmnt")
            .args(["-n", "-o", "SOURCE", "--mountpoint", mp])
            .output()
            .map_err(|e| e.to_string())?;
        let s = String::from_utf8_lossy(&o.stdout).trim().to_string();
        fs::canonicalize(&s).map_err(|_| format!("Cannot resolve source {s}"))
    };
    let root_src = src_of("/")?;
    let boot_src = src_of(BOOT_DIR)?;
    let root_s = root_src.to_string_lossy();
    let Some(base_disk) = root_s.strip_suffix("p2") else {
        return Err("Refusing OTA: root is not an eMMC partition 2".into());
    };
    if !base_disk.starts_with("/dev/mmcblk") || boot_src.to_string_lossy() != format!("{base_disk}p1") {
        return Err("Refusing OTA: /boot must be BOOT on the same eMMC as root".into());
    }
    let disk_name = base_disk.trim_start_matches("/dev/");
    let dev_type = fs::read_to_string(format!("/sys/class/block/{disk_name}/device/type"))
        .unwrap_or_default()
        .trim()
        .to_string();
    let label = Command::new("blkid")
        .args(["-s", "LABEL", "-o", "value", &boot_src.to_string_lossy()])
        .output()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .unwrap_or_default();
    if dev_type != "MMC" || label != "BOOT" {
        return Err("Refusing OTA: /boot must be BOOT on the same eMMC as root".into());
    }
    Ok(())
}

fn rootfs_destination(rel: &Path) -> Result<PathBuf, String> {
    let clean = safe_rel(&rel.to_string_lossy())?;
    let parts: Vec<&str> = clean.split('/').collect();
    let logical = if parts.len() >= 3 && parts[0] == "usr" && parts[1] == "lib" && (parts[2] == "modules" || parts[2] == "firmware") {
        format!("lib/{}", parts[2..].join("/"))
    } else {
        clean
    };
    let top = logical.split('/').next().unwrap_or("");
    if matches!(top, "proc" | "sys" | "dev" | "run" | "tmp" | "boot" | "")
        || logical.starts_with("var/run/")
        || logical.starts_with("var/lib/atrios/ota-")
    {
        return Err(format!("Forbidden rootfs update destination: {logical}"));
    }
    let root_canon = fs::canonicalize(ROOT_DIR).map_err(|e| e.to_string())?;
    let target = Path::new(ROOT_DIR).join(&logical);
    // Resolve existing ancestor to verify no symlink escapes root
    let mut probe = target.parent();
    while let Some(p) = probe {
        if p.exists() {
            let pc = fs::canonicalize(p).map_err(|e| e.to_string())?;
            if !pc.starts_with(&root_canon) {
                return Err("Rootfs destination escapes target root".into());
            }
            break;
        }
        probe = p.parent();
    }
    Ok(target)
}

fn collect_files(dir: &Path, out: &mut Vec<PathBuf>) -> Result<(), String> {
    let mut entries: Vec<PathBuf> = fs::read_dir(dir)
        .map_err(|e| e.to_string())?
        .flatten()
        .map(|e| e.path())
        .collect();
    entries.sort();
    for p in entries {
        if p.is_dir() {
            collect_files(&p, out)?;
        } else if p.is_file() {
            out.push(p);
        }
    }
    Ok(())
}

#[derive(Clone, Debug)]
pub struct OtaStatus {
    pub state: String,
    pub current_version: String,
    pub latest_version: Option<String>,
    pub latest_tag: Option<String>,
    pub update_available: bool,
    pub progress: u8,
    pub stage: String,
    pub message: String,
    pub error: Option<String>,
    pub changelog: String,
    pub asset_size: u64,
    pub downloaded_bytes: u64,
    pub rollback_available: bool,
    pub last_check_ts: i64,
    pub updated_ts: i64,
    pub reboot_required: bool,
}

impl OtaStatus {
    pub fn default_idle() -> Self {
        Self {
            state: "idle".into(),
            current_version: current_version(),
            latest_version: None,
            latest_tag: None,
            update_available: false,
            progress: 0,
            stage: "idle".into(),
            message: String::new(),
            error: None,
            changelog: String::new(),
            asset_size: 0,
            downloaded_bytes: 0,
            rollback_available: rollback_available_at(Path::new(JOURNAL)),
            last_check_ts: 0,
            updated_ts: now_unix_i64(),
            reboot_required: false,
        }
    }

    pub fn from_json(j: &Json) -> Self {
        let mut st = Self::default_idle();
        if let Some(s) = j.get_str("state").or_else(|| j.get_str("status")) {
            if !s.is_empty() {
                st.state = s.into();
            }
        }
        if let Some(s) = j.get_str("latest_version") {
            if !s.is_empty() {
                st.latest_version = Some(s.into());
            }
        } else if let Some(s) = j.get_str("version") {
            if !s.is_empty() && st.state != "installed" && st.state != "rolled_back" {
                st.latest_version = Some(s.into());
            }
        }
        if let Some(s) = j.get_str("latest_tag").or_else(|| j.get_str("tag")) {
            if !s.is_empty() {
                st.latest_tag = Some(s.into());
            }
        }
        if let Some(b) = j.get("update_available").and_then(|v| v.as_bool()) {
            st.update_available = b;
        }
        if let Some(p) = j.get("progress").and_then(|v| v.as_i64()) {
            st.progress = p.clamp(0, 100) as u8;
        }
        if let Some(s) = j.get_str("stage") {
            st.stage = s.into();
        }
        if let Some(s) = j.get_str("message") {
            st.message = s.into();
        }
        if let Some(s) = j.get_str("error") {
            if !s.is_empty() {
                st.error = Some(s.into());
            }
        }
        if let Some(s) = j.get_str("changelog") {
            st.changelog = s.into();
        }
        if let Some(n) = j.get("asset_size").or_else(|| j.get("size")).and_then(|v| v.as_f64()) {
            if n >= 0.0 {
                st.asset_size = n as u64;
            }
        }
        if let Some(n) = j.get("downloaded_bytes").and_then(|v| v.as_f64()) {
            if n >= 0.0 {
                st.downloaded_bytes = n as u64;
            }
        }
        if let Some(n) = j.get("last_check_ts").and_then(|v| v.as_i64()) {
            st.last_check_ts = n;
        }
        if let Some(n) = j.get("updated_ts").and_then(|v| v.as_i64()) {
            st.updated_ts = n;
        }
        if let Some(b) = j.get("reboot_required").and_then(|v| v.as_bool()) {
            st.reboot_required = b;
        }
        st.current_version = current_version();
        st.rollback_available = rollback_available_at(Path::new(JOURNAL));
        st
    }

    pub fn to_json(&self) -> Json {
        obj(vec![
            ("state", Json::Str(self.state.clone())),
            ("current_version", Json::Str(self.current_version.clone())),
            (
                "latest_version",
                self.latest_version.as_ref().map(|s| Json::Str(s.clone())).unwrap_or(Json::Null),
            ),
            (
                "latest_tag",
                self.latest_tag.as_ref().map(|s| Json::Str(s.clone())).unwrap_or(Json::Null),
            ),
            ("update_available", Json::Bool(self.update_available)),
            ("progress", Json::Num(self.progress as f64)),
            ("stage", Json::Str(self.stage.clone())),
            ("message", Json::Str(self.message.clone())),
            (
                "error",
                self.error.as_ref().map(|s| Json::Str(s.clone())).unwrap_or(Json::Null),
            ),
            ("changelog", Json::Str(self.changelog.clone())),
            ("asset_size", Json::Num(self.asset_size as f64)),
            ("downloaded_bytes", Json::Num(self.downloaded_bytes as f64)),
            ("rollback_available", Json::Bool(self.rollback_available)),
            ("last_check_ts", Json::Num(self.last_check_ts as f64)),
            ("updated_ts", Json::Num(self.updated_ts as f64)),
            ("reboot_required", Json::Bool(self.reboot_required)),
        ])
    }
}

pub fn rollback_available_at(journal_path: &Path) -> bool {
    let Ok(txt) = fs::read_to_string(journal_path) else {
        return false;
    };
    let Ok(j) = Json::parse(&txt) else {
        return false;
    };
    if !matches!(j.get_str("status"), Some("installed" | "installing" | "rolling_back")) {
        return false;
    }
    let Some(bdir) = j.get_str("backup_dir") else {
        return false;
    };
    Path::new(bdir).is_dir()
}

pub fn rollback_available() -> bool {
    rollback_available_at(Path::new(JOURNAL))
}

pub fn read_ota_status() -> OtaStatus {
    for p in [RUNTIME_STATUS_FILE, STATE_FILE] {
        if let Ok(txt) = fs::read_to_string(p) {
            if let Ok(j) = Json::parse(&txt) {
                return OtaStatus::from_json(&j);
            }
        }
    }
    OtaStatus::default_idle()
}

pub fn read_ota_status_json() -> Json {
    read_ota_status().to_json()
}

fn write_ota_status(st: &mut OtaStatus, persist: bool) {
    st.current_version = current_version();
    st.rollback_available = rollback_available_at(Path::new(JOURNAL));
    st.updated_ts = now_unix_i64();
    let j = st.to_json();
    let mut s = j.to_string_pretty();
    s.push('\n');
    let bytes = s.as_bytes();
    if let Some(parent) = Path::new(RUNTIME_STATUS_FILE).parent() {
        let _ = fs::create_dir_all(parent);
    }
    let _ = atomic_write_bytes(Path::new(RUNTIME_STATUS_FILE), bytes, 0o644, false);
    if persist {
        if let Some(parent) = Path::new(STATE_FILE).parent() {
            let _ = fs::create_dir_all(parent);
        }
        let _ = atomic_write_bytes(Path::new(STATE_FILE), bytes, 0o644, true);
    }
}

pub fn is_ota_busy() -> bool {
    let Ok(file) = OpenOptions::new().create(true).append(true).open(LOCK_FILE) else {
        return false;
    };
    let rc = unsafe { flock(file.as_raw_fd(), LOCK_EX | LOCK_NB) };
    rc != 0
}

pub fn mark_ota_starting(state: &str, stage: &str, message: &str) -> Json {
    let mut st = read_ota_status();
    st.state = state.into();
    st.stage = stage.into();
    st.progress = match state {
        "checking" => 5,
        "downloading" => 5,
        "rolling_back" => 10,
        _ => 0,
    };
    st.message = message.into();
    st.error = None;
    write_ota_status(&mut st, false);
    st.to_json()
}

fn cleanup_partial_tmps(dir: &Path) {
    let Ok(rd) = fs::read_dir(dir) else { return };
    for entry in rd.flatten() {
        let name = entry.file_name();
        let ns = name.to_string_lossy();
        if ns.starts_with(".tmp.") || ns.ends_with(".tmp") {
            let _ = fs::remove_file(entry.path());
        }
    }
}

pub(crate) fn rollback_transaction_at(journal_path: &Path, mount_boot: bool) -> Result<(), String> {
    if !journal_path.exists() {
        return Err("No OTA transaction available for rollback".into());
    }
    let txt = fs::read_to_string(journal_path).map_err(|e| e.to_string())?;
    let mut state = Json::parse(&txt)?;
    if mount_boot && state.get("boot").and_then(|v| v.as_bool()) == Some(true) {
        ensure_boot_mounted()?;
    }
    if let Json::Obj(ref mut m) = state {
        m.insert("status".into(), Json::Str("rolling_back".into()));
    }
    atomic_json(journal_path, &state)?;

    let backup_dir = PathBuf::from(state.get_str("backup_dir").ok_or("Missing backup_dir")?);
    let mut errors: Vec<String> = Vec::new();
    let mut parent_dirs: BTreeSet<PathBuf> = BTreeSet::new();

    if let Some(entries) = state.get("entries").and_then(|v| v.as_arr()) {
        for entry in entries.iter().rev() {
            let dest = PathBuf::from(entry.get_str("destination").unwrap_or(""));
            if dest.as_os_str().is_empty() {
                continue;
            }
            if let Some(p) = dest.parent() {
                parent_dirs.insert(p.to_path_buf());
            }
            let existed = entry.get("existed").and_then(|v| v.as_bool()).unwrap_or(false);
            let backup = entry.get_str("backup").unwrap_or("");
            let expected_bak_sha = entry.get_str("backup_sha256").filter(|s| !s.is_empty());

            if existed {
                let bpath = backup_dir.join(backup);
                if !bpath.is_file() {
                    errors.push(format!("Missing backup file for {}", dest.display()));
                    continue;
                }
                if let Some(want_sha) = expected_bak_sha {
                    match sha256_file_hex(&bpath) {
                        Ok(got) if got.eq_ignore_ascii_case(want_sha) => {}
                        Ok(_) => {
                            errors.push(format!("Backup checksum mismatch for {}", dest.display()));
                            continue;
                        }
                        Err(e) => {
                            errors.push(format!("Backup read error for {}: {e}", dest.display()));
                            continue;
                        }
                    }
                }
                if let Err(e) = atomic_copy(&bpath, &dest) {
                    errors.push(format!("Failed restoring {}: {e}", dest.display()));
                    continue;
                }
                if let Some(want_sha) = expected_bak_sha {
                    match sha256_file_hex(&dest) {
                        Ok(got) if got.eq_ignore_ascii_case(want_sha) => {}
                        Ok(_) => errors.push(format!("Restored checksum mismatch for {}", dest.display())),
                        Err(e) => errors.push(format!("Restored verify error for {}: {e}", dest.display())),
                    }
                }
            } else if dest.exists() {
                if let Err(e) = fs::remove_file(&dest) {
                    errors.push(format!("Failed removing {}: {e}", dest.display()));
                } else if let Some(p) = dest.parent() {
                    let _ = sync_dir(p);
                }
            }
        }
    }

    for dir in &parent_dirs {
        cleanup_partial_tmps(dir);
    }

    if !errors.is_empty() {
        return Err(format!("Rollback completed with errors: {}", errors.join("; ")));
    }

    let prev = state.get("previous").cloned();
    if let Json::Obj(ref mut m) = state {
        m.insert("status".into(), Json::Str("rolled_back".into()));
    }
    let to_write = match prev {
        Some(p) if !matches!(p, Json::Null) => p,
        _ => state.clone(),
    };
    atomic_json(journal_path, &to_write)?;

    let prev_bdir = to_write.get_str("backup_dir").map(PathBuf::from);
    if prev_bdir.as_ref() != Some(&backup_dir) && backup_dir.is_dir() {
        let _ = fs::remove_dir_all(&backup_dir);
    }
    Ok(())
}

fn rollback_transaction() -> Result<(), String> {
    let mut st = read_ota_status();
    st.state = "rolling_back".into();
    st.stage = "rolling_back".into();
    st.progress = 50;
    st.message = "Rolling back previous firmware files...".into();
    write_ota_status(&mut st, false);

    match rollback_transaction_at(Path::new(JOURNAL), true) {
        Ok(()) => {
            st.state = "rolled_back".into();
            st.stage = "rolled_back".into();
            st.progress = 100;
            st.message = format!("Rolled back to version {}", current_version());
            st.error = None;
            st.reboot_required = false;
            write_ota_status(&mut st, true);
            set_error("ota", None, "", 120);
            Ok(())
        }
        Err(e) => {
            st.state = "error".into();
            st.stage = "failed".into();
            st.message = format!("Rollback failed: {e}");
            st.error = Some(e.clone());
            write_ota_status(&mut st, true);
            Err(e)
        }
    }
}

pub(crate) fn apply_plan_transactional(
    plan: &[(PathBuf, PathBuf)],
    journal_path: &Path,
    backup_base: &Path,
    has_boot: bool,
    version: &str,
    source_label: &str,
    mount_boot_on_rollback: bool,
    fail_after_install_idx: Option<usize>,
    mut on_progress: Option<&mut dyn FnMut(u8, &str, &str)>,
) -> Result<(), String> {
    if plan.is_empty() {
        return Err("OTA bundle contains no installable files".into());
    }
    if journal_path.exists() {
        if let Ok(j) = Json::parse(&fs::read_to_string(journal_path).unwrap_or_default()) {
            if matches!(j.get_str("status"), Some("installing" | "rolling_back")) {
                return Err("Interrupted update exists; run atrios ota recover first".into());
            }
        }
    }

    // Pre-compute source SHA-256 digests before touching anything on disk
    let mut src_hashes: Vec<String> = Vec::with_capacity(plan.len());
    for (src, _) in plan {
        let h = sha256_file_hex(src).map_err(|e| format!("Failed hashing source {}: {e}", src.display()))?;
        src_hashes.push(h);
    }

    let previous = if journal_path.exists() {
        Json::parse(&fs::read_to_string(journal_path).unwrap_or_default()).ok()
    } else {
        None
    };

    let bparent = backup_base.parent().unwrap_or_else(|| Path::new("/var/lib/atrios"));
    fs::create_dir_all(bparent).map_err(|e| e.to_string())?;
    let tx_backup = PathBuf::from(format!(
        "{}-{}-{}",
        backup_base.to_string_lossy(),
        std::process::id(),
        now_unix_i64()
    ));
    fs::create_dir_all(&tx_backup).map_err(|e| e.to_string())?;

    let build_state = |status_str: &str, ents: &[Json], prev: Option<Json>| {
        let mut m = BTreeMap::new();
        m.insert("status".into(), Json::Str(status_str.into()));
        if let Some(p) = prev {
            m.insert("previous".into(), p);
        }
        m.insert("backup_dir".into(), Json::Str(tx_backup.to_string_lossy().into_owned()));
        m.insert("version".into(), Json::Str(version.into()));
        m.insert("source".into(), Json::Str(source_label.into()));
        m.insert("boot".into(), Json::Bool(has_boot));
        m.insert("entries".into(), Json::Arr(ents.to_vec()));
        Json::Obj(m)
    };

    // Phase 1: Stage and verify ALL backups BEFORE overwriting ANY target file
    if let Some(cb) = on_progress.as_deref_mut() {
        cb(82, "backing_up", "Staging and verifying rollback backups...");
    }
    let mut entries_json: Vec<Json> = Vec::with_capacity(plan.len());
    let stage_res: Result<(), String> = (|| {
        for (idx, (_, dst)) in plan.iter().enumerate() {
            let existed = dst.exists();
            let bname = idx.to_string();
            let mut backup_sha = String::new();
            if existed {
                let orig_sha = sha256_file_hex(dst)
                    .map_err(|e| format!("Failed hashing existing {}: {e}", dst.display()))?;
                let bpath = tx_backup.join(&bname);
                atomic_copy(dst, &bpath)?;
                let verified_sha = sha256_file_hex(&bpath)
                    .map_err(|e| format!("Failed verifying backup for {}: {e}", dst.display()))?;
                if verified_sha != orig_sha {
                    return Err(format!("Backup checksum mismatch for {}", dst.display()));
                }
                backup_sha = orig_sha;
            }
            entries_json.push(obj(vec![
                ("destination", Json::Str(dst.to_string_lossy().into_owned())),
                ("existed", Json::Bool(existed)),
                ("backup", Json::Str(bname)),
                ("backup_sha256", Json::Str(backup_sha)),
                ("expected_sha256", Json::Str(src_hashes[idx].clone())),
            ]));
        }
        Ok(())
    })();

    if let Err(e) = stage_res {
        let _ = fs::remove_dir_all(&tx_backup);
        return Err(e);
    }

    // Commit staged journal BEFORE overwriting any target file
    if let Err(e) = atomic_json(journal_path, &build_state("installing", &entries_json, previous.clone())) {
        let _ = fs::remove_dir_all(&tx_backup);
        return Err(e);
    }

    // Phase 2: Atomic install + post-write SHA-256 verification
    let total = plan.len().max(1);
    let tx_res: Result<(), String> = (|| {
        for (idx, (src, dst)) in plan.iter().enumerate() {
            if fail_after_install_idx == Some(idx) {
                return Err(format!("Injected install failure at index {idx} ({})", dst.display()));
            }
            atomic_copy(src, dst)?;
            let written_sha = sha256_file_hex(dst)
                .map_err(|e| format!("Post-install read failed for {}: {e}", dst.display()))?;
            if written_sha != src_hashes[idx] {
                return Err(format!("Post-install SHA-256 mismatch for {}", dst.display()));
            }
            if let Some(cb) = on_progress.as_deref_mut() {
                let pct = 85 + (((idx + 1) * 13) / total) as u8;
                cb(pct.min(98), "installing", "Installing verified update files...");
            }
        }
        atomic_json(journal_path, &build_state("installed", &entries_json, None))?;
        if let Some(ref p) = previous {
            if let Some(old) = p.get_str("backup_dir") {
                let op = PathBuf::from(old);
                if op != tx_backup && op.parent() == Some(bparent) {
                    let _ = fs::remove_dir_all(op);
                }
            }
        }
        Ok(())
    })();

    if let Err(install_err) = tx_res {
        if let Some(cb) = on_progress.as_deref_mut() {
            cb(90, "rolling_back", "Install failed; automatically restoring backups...");
        }
        match rollback_transaction_at(journal_path, mount_boot_on_rollback) {
            Ok(()) => {
                return Err(format!("Install failed (automatically rolled back): {install_err}"));
            }
            Err(rb_err) => {
                return Err(format!("Install failed ({install_err}); rollback error: {rb_err}"));
            }
        }
    }
    Ok(())
}

fn install_bundle_tar(
    pkg_path: &Path,
    source_label: &str,
    do_reboot: bool,
    expected_version: Option<&str>,
) -> Result<(), String> {
    let cfg = load_config()?;
    let mut st = read_ota_status();
    st.state = "verifying".into();
    st.stage = "verifying".into();
    st.progress = 76;
    st.message = "Extracting and verifying OTA package...".into();
    st.error = None;
    write_ota_status(&mut st, false);

    if Path::new(JOURNAL).exists() {
        if let Ok(j) = Json::parse(&fs::read_to_string(JOURNAL).unwrap_or_default()) {
            if matches!(j.get_str("status"), Some("installing" | "rolling_back")) {
                return Err("Interrupted update exists; run atrios ota recover first".into());
            }
        }
    }

    let tmp_dir = PathBuf::from(format!("/tmp/atrios-ota-{}", std::process::id()));
    let _ = fs::remove_dir_all(&tmp_dir);
    let work = tmp_dir.join("payload");
    fs::create_dir_all(&work).map_err(|e| e.to_string())?;

    let mut installed_version = String::new();
    let mut rolled_back = false;

    let install_res: Result<(), String> = (|| {
        extract_safe(pkg_path, &work)?;
        let (manifest, payloads) = verify_bundle(&work, &cfg)?;
        validate_boot_payloads(&payloads)?;
        let m_ver = manifest.get_str("version").unwrap_or("");
        if version_key(m_ver)? < version_key(&current_version())? {
            return Err("Refusing an OTA downgrade".into());
        }
        if let Some(ev) = expected_version {
            if m_ver.strip_prefix('v').unwrap_or(m_ver) != ev.strip_prefix('v').unwrap_or(ev) {
                return Err("Signed manifest version does not match GitHub release".into());
            }
        }
        installed_version = m_ver.to_string();

        st.progress = 80;
        st.message = "Preparing installation plan...".into();
        write_ota_status(&mut st, false);

        let mut plan: Vec<(PathBuf, PathBuf)> = Vec::new();
        if let Some(rf) = payloads.get("rootfs") {
            let overlay = tmp_dir.join("overlay");
            fs::create_dir_all(&overlay).map_err(|e| e.to_string())?;
            extract_safe(rf, &overlay)?;
            let mut src_files = Vec::new();
            collect_files(&overlay, &mut src_files)?;
            for src in src_files {
                let rel = src.strip_prefix(&overlay).map_err(|e| e.to_string())?;
                let dst = rootfs_destination(rel)?;
                plan.push((src, dst));
            }
        }
        let has_boot = payloads.contains_key("kernel") || payloads.contains_key("dtb");
        if let Some(kf) = payloads.get("kernel") {
            let rel = manifest.get_str("kernel_release").unwrap_or("");
            if !valid_tag_str(rel, 128) {
                return Err("kernel_release is required".into());
            }
            let (cur_rel, _) = uname_strings();
            if rel != cur_rel {
                let needle = format!("/modules/{rel}/");
                if !plan.iter().any(|(_, d)| d.to_string_lossy().contains(&needle)) {
                    return Err("New kernel release requires matching modules in rootfs overlay".into());
                }
            }
            plan.push((kf.clone(), Path::new(BOOT_DIR).join("Image")));
        }
        if let Some(df) = payloads.get("dtb") {
            let name = manifest.get_str("dtb_name").unwrap_or("meson-sm1-yandex-station-max.dtb");
            if safe_rel(name)? != name || name.contains('/') || !name.ends_with(".dtb") {
                return Err("Invalid DTB name".into());
            }
            plan.push((df.clone(), Path::new(BOOT_DIR).join(name)));
            plan.push((df.clone(), Path::new(BOOT_DIR).join("dtb.img")));
        }
        if plan.is_empty() {
            return Err("OTA bundle contains no installable files".into());
        }
        if has_boot {
            ensure_boot_mounted()?;
        }

        let mut seen_dsts = BTreeSet::new();
        for (_, dst) in &plan {
            let ds = dst.to_string_lossy().into_owned();
            if !seen_dsts.insert(ds) {
                return Err("Duplicate OTA destinations".into());
            }
            if fs::symlink_metadata(dst).is_ok_and(|m| m.file_type().is_symlink() || !m.is_file()) {
                return Err(format!("Refusing non-regular OTA destination: {}", dst.display()));
            }
        }
        let ver_dst = PathBuf::from(VERSION_FILE);
        if !seen_dsts.insert(VERSION_FILE.into()) {
            return Err("Duplicate OTA destinations".into());
        }

        let required_bak: u64 = plan
            .iter()
            .filter_map(|(_, d)| fs::metadata(d).ok().map(|m| m.len()))
            .sum();
        let bparent = Path::new(BACKUP_DIR).parent().unwrap();
        fs::create_dir_all(bparent).map_err(|e| e.to_string())?;
        if free_bytes(bparent).map_err(|e| e.to_string())? < required_bak + 16 * 1024 * 1024 {
            return Err("ENOSPC: Insufficient rollback storage".into());
        }
        for (src, dst) in &plan {
            let mut par = dst.parent();
            while let Some(p) = par {
                if p.exists() {
                    let src_sz = fs::metadata(src).map(|m| m.len()).unwrap_or(0);
                    if free_bytes(p).map_err(|e| e.to_string())? < src_sz + 1024 * 1024 {
                        return Err(format!("ENOSPC: Insufficient space for {}", dst.display()));
                    }
                    break;
                }
                par = p.parent();
            }
        }

        let ver_src = tmp_dir.join("new-version");
        fs::write(&ver_src, format!("{m_ver}\n")).map_err(|e| e.to_string())?;
        plan.push((ver_src, ver_dst));

        let mut progress_cb = |pct: u8, stage: &str, msg: &str| {
            st.state = if stage == "rolling_back" { "rolling_back" } else { "installing" }.into();
            st.stage = stage.into();
            st.progress = pct;
            st.message = msg.into();
            write_ota_status(&mut st, false);
        };

        let res = apply_plan_transactional(
            &plan,
            Path::new(JOURNAL),
            Path::new(BACKUP_DIR),
            has_boot,
            m_ver,
            source_label,
            true,
            None,
            Some(&mut progress_cb),
        );
        if let Err(ref e) = res {
            if e.contains("automatically rolled back") {
                rolled_back = true;
            }
        }
        res
    })();

    let _ = fs::remove_dir_all(&tmp_dir);

    match install_res {
        Ok(()) => {
            set_error("ota", None, "", 120);
            st.state = "installed".into();
            st.stage = "done".into();
            st.progress = 100;
            st.current_version = installed_version.clone();
            st.update_available = false;
            st.error = None;
            st.reboot_required = !do_reboot;
            st.message = if do_reboot {
                format!("Installed version {installed_version}; rebooting...")
            } else {
                format!("Installed version {installed_version}; reboot to apply")
            };
            write_ota_status(&mut st, true);
            if do_reboot {
                unsafe {
                    sync();
                    reboot(RB_AUTOBOOT);
                }
            }
            Ok(())
        }
        Err(e) => {
            st.state = if rolled_back { "rolled_back" } else { "error" }.into();
            st.stage = if rolled_back { "rolled_back" } else { "failed" }.into();
            st.progress = 0;
            st.message = e.clone();
            st.error = Some(e.clone());
            write_ota_status(&mut st, true);
            Err(e)
        }
    }
}

struct ReleaseInfo {
    tag: String,
    version: String,
    asset: String,
    url: String,
    size: u64,
    sha256: String,
    changelog: String,
}

fn release_to_json(r: &ReleaseInfo, status: &str, path: &Path) -> Json {
    obj(vec![
        ("tag", Json::Str(r.tag.clone())),
        ("version", Json::Str(r.version.clone())),
        ("asset", Json::Str(r.asset.clone())),
        ("url", Json::Str(r.url.clone())),
        ("size", Json::Num(r.size as f64)),
        ("sha256", Json::Str(r.sha256.clone())),
        ("changelog", Json::Str(r.changelog.clone())),
        ("status", Json::Str(status.into())),
        ("path", Json::Str(path.to_string_lossy().into_owned())),
    ])
}

fn latest_release(cfg: &OtaConfig) -> Result<ReleaseInfo, String> {
    let hdrs = [("X-GitHub-Api-Version", "2022-11-28")];
    let latest_url = format!("https://api.github.com/repos/{}/releases/latest", cfg.repository);
    let mut data = request_json(&latest_url, None, &hdrs, None, CHECK_TIMEOUT_SECS, 1024 * 1024, None)?;
    let board = device_id()?;
    let asset_name = format!("AtriOS-{board}-ota.tar.gz");

    if data.get_str("tag_name").unwrap_or("").starts_with("app-") {
        let list_url = format!("https://api.github.com/repos/{}/releases?per_page=100", cfg.repository);
        let raw = request_bytes(&list_url, None, &hdrs, None, CHECK_TIMEOUT_SECS, 4 * 1024 * 1024, None)?;
        let list = Json::parse(&String::from_utf8_lossy(&raw))?;
        let Some(arr) = list.as_arr() else { return Err("Invalid GitHub release list".into()); };
        let mut best: Option<([u64; 4], Json)> = None;
        for rel in arr {
            if rel.get("draft").and_then(|v| v.as_bool()) == Some(true)
                || rel.get("prerelease").and_then(|v| v.as_bool()) == Some(true)
            {
                continue;
            }
            let Ok(rank) = version_key(rel.get_str("tag_name").unwrap_or("")) else { continue };
            let has_asset = rel
                .get("assets")
                .and_then(|a| a.as_arr())
                .map(|xs| xs.iter().any(|x| x.get_str("name") == Some(&asset_name)))
                .unwrap_or(false);
            if has_asset && best.as_ref().map(|b| rank > b.0).unwrap_or(true) {
                best = Some((rank, rel.clone()));
            }
        }
        data = best.ok_or("No stable firmware release is published for this board")?.1;
    }

    if data.get("draft").and_then(|v| v.as_bool()) == Some(true)
        || data.get("prerelease").and_then(|v| v.as_bool()) == Some(true)
    {
        return Err("Only stable published releases are accepted".into());
    }
    let assets = data.get("assets").and_then(|a| a.as_arr()).ok_or("Release has no assets")?;
    let asset = assets
        .iter()
        .find(|a| a.get_str("name") == Some(&asset_name))
        .ok_or_else(|| format!("Release has no bundle for this board: {asset_name}"))?;
    let url = asset.get_str("browser_download_url").unwrap_or("").to_string();
    let parsed = require_https(&url)?;
    let expected_prefix = format!("/{}/releases/download/", cfg.repository);
    if parsed.host != "github.com" || !parsed.path_query.starts_with(&expected_prefix) {
        return Err("Unexpected GitHub release asset URL".into());
    }
    let size = asset.get("size").and_then(|v| v.as_f64()).map(|n| n as u64).unwrap_or(0);
    if size == 0 || size > MAX_DOWNLOAD {
        return Err("Invalid release asset size".into());
    }
    let tag = data.get_str("tag_name").unwrap_or("").to_string();
    if !valid_tag_str(&tag, 64) {
        return Err("Invalid release tag".into());
    }
    let digest_str = asset.get_str("digest").unwrap_or("").to_string();
    let sha = if let Some(hex) = digest_str.strip_prefix("sha256:") {
        if hex.len() == 64 && hex.bytes().all(|b| b.is_ascii_hexdigit()) {
            hex.to_ascii_lowercase()
        } else {
            return Err("Invalid digest in release".into());
        }
    } else {
        let side_name = format!("{asset_name}.sha256");
        let side = assets
            .iter()
            .find(|a| a.get_str("name") == Some(&side_name))
            .ok_or("Release has no SHA-256 digest")?;
        let side_url = format!("{url}.sha256");
        if side.get_str("browser_download_url") != Some(&side_url) {
            return Err("Release has no SHA-256 digest".into());
        }
        let body = request_bytes(&side_url, None, &[], None, CHECK_TIMEOUT_SECS, 1024, None)?;
        let text = String::from_utf8_lossy(&body);
        let tok = text.split_whitespace().next().unwrap_or("");
        if tok.len() != 64 || !tok.bytes().all(|b| b.is_ascii_hexdigit()) {
            return Err("Invalid release checksum file".into());
        }
        tok.to_ascii_lowercase()
    };
    let ver = tag.strip_prefix('v').unwrap_or(&tag).to_string();
    let changelog: String = data
        .get_str("body")
        .unwrap_or("")
        .trim()
        .chars()
        .take(1500)
        .collect();
    Ok(ReleaseInfo { tag, version: ver, asset: asset_name, url, size, sha256: sha, changelog })
}

fn download_release(rel: &ReleaseInfo) -> Result<PathBuf, String> {
    fs::create_dir_all(CACHE_DIR).map_err(|e| e.to_string())?;
    let target = Path::new(CACHE_DIR).join(format!("{}-{}", rel.tag, rel.asset));

    let mut st = read_ota_status();
    st.state = "downloading".into();
    st.stage = "downloading".into();
    st.latest_version = Some(rel.version.clone());
    st.latest_tag = Some(rel.tag.clone());
    st.changelog = rel.changelog.clone();
    st.asset_size = rel.size;
    st.error = None;

    if target.is_file() && sha256_file_hex(&target).ok().as_deref() == Some(&rel.sha256) {
        st.progress = 75;
        st.downloaded_bytes = rel.size;
        st.message = format!("Using verified cached bundle {}", rel.tag);
        write_ota_status(&mut st, false);
        return Ok(target);
    }
    if free_bytes(Path::new(CACHE_DIR)).map_err(|e| e.to_string())? < rel.size + 16 * 1024 * 1024 {
        return Err("ENOSPC: Insufficient update download storage".into());
    }
    st.progress = 5;
    st.downloaded_bytes = 0;
    st.message = format!("Downloading {}...", rel.tag);
    write_ota_status(&mut st, false);

    let tmp = Path::new(CACHE_DIR).join(format!(".download-{}.tmp", std::process::id()));
    let res: Result<PathBuf, String> = (|| {
        let mut out = File::create(&tmp).map_err(|e| e.to_string())?;
        let mut hasher = Sha256::new();
        let mut got_size = 0u64;
        let mut last_report_bytes = 0u64;
        let mut last_report_ts = Instant::now();
        let cancel_fn = || STOP_REQUESTED.load(Ordering::SeqCst);
        https_stream(
            &rel.url,
            "GET",
            None,
            &[],
            None,
            1800,
            rel.size.min(MAX_DOWNLOAD) as usize,
            Some(&cancel_fn),
            |chunk| {
                got_size += chunk.len() as u64;
                if got_size > rel.size || got_size > MAX_DOWNLOAD {
                    return Err("Oversized release download".into());
                }
                out.write_all(chunk).map_err(|e| e.to_string())?;
                hasher.update(chunk);
                if got_size.saturating_sub(last_report_bytes) >= 256 * 1024
                    || last_report_ts.elapsed() >= Duration::from_millis(500)
                {
                    last_report_bytes = got_size;
                    last_report_ts = Instant::now();
                    let pct = if rel.size > 0 {
                        (5 + ((got_size.saturating_mul(70)) / rel.size)) as u8
                    } else {
                        40
                    };
                    st.progress = pct.min(75);
                    st.downloaded_bytes = got_size;
                    write_ota_status(&mut st, false);
                }
                Ok(())
            },
        )?;
        out.flush().map_err(|e| e.to_string())?;
        out.sync_all().map_err(|e| e.to_string())?;
        let digest = hex_lower(&hasher.finalize());
        if got_size != rel.size || digest != rel.sha256 {
            return Err("Release download SHA-256/size mismatch".into());
        }
        fs::rename(&tmp, &target).map_err(|e| e.to_string())?;
        let _ = sync_dir(Path::new(CACHE_DIR));
        st.progress = 75;
        st.downloaded_bytes = got_size;
        st.message = format!("Downloaded and verified {}", rel.tag);
        write_ota_status(&mut st, true);
        set_error("ota", None, "", 120);
        Ok(target.clone())
    })();
    if let Err(ref e) = res {
        let _ = fs::remove_file(&tmp);
        st.state = "error".into();
        st.stage = "failed".into();
        st.message = e.clone();
        st.error = Some(e.clone());
        write_ota_status(&mut st, true);
    }
    res
}

fn perform_check() -> Result<(OtaStatus, Option<ReleaseInfo>), String> {
    set_error("ota", None, "", 120);
    let mut st = read_ota_status();
    st.state = "checking".into();
    st.stage = "checking".into();
    st.progress = 10;
    st.message = "Checking for firmware updates...".into();
    st.error = None;
    write_ota_status(&mut st, false);

    let cfg = load_config()?;
    match latest_release(&cfg) {
        Ok(rel) => {
            let cur = current_version();
            let avail = version_key(&rel.version)? > version_key(&cur)?;
            st.current_version = cur;
            st.latest_version = Some(rel.version.clone());
            st.latest_tag = Some(rel.tag.clone());
            st.update_available = avail;
            st.changelog = rel.changelog.clone();
            st.asset_size = rel.size;
            st.downloaded_bytes = 0;
            st.last_check_ts = now_unix_i64();
            st.error = None;
            st.progress = 100;
            if avail {
                st.state = "available".into();
                st.stage = "available".into();
                st.message = format!("Update {} is available", rel.version);
            } else {
                st.state = "up_to_date".into();
                st.stage = "idle".into();
                st.message = "AtriOS is up to date".into();
            }
            write_ota_status(&mut st, true);
            Ok((st, Some(rel)))
        }
        Err(e) => {
            st.last_check_ts = now_unix_i64();
            if e.contains("No stable firmware release") {
                st.state = "up_to_date".into();
                st.stage = "idle".into();
                st.update_available = false;
                st.progress = 100;
                st.message = "No newer stable firmware release published".into();
                st.error = None;
                write_ota_status(&mut st, true);
                return Ok((st, None));
            }
            // Non-blocking error: record quietly in ota_status.json, NEVER hijack LED screen!
            st.state = "error".into();
            st.stage = "failed".into();
            st.progress = 0;
            st.message = format!("Update check failed: {e}");
            st.error = Some(e.clone());
            write_ota_status(&mut st, true);
            Err(e)
        }
    }
}

fn check_and_download(install: bool, do_reboot: bool) -> Result<(), String> {
    let (st, maybe_rel) = perform_check()?;
    let Some(rel) = maybe_rel else {
        println!("AtriOS is up to date");
        return Ok(());
    };
    if !st.update_available {
        println!("AtriOS is up to date");
        return Ok(());
    }
    let path = download_release(&rel)?;
    println!("Downloaded: {}", path.display());
    if install {
        install_bundle_tar(&path, &rel.url, do_reboot, Some(&rel.version))?;
    }
    Ok(())
}

fn sleep_interruptible(secs: u64) {
    let deadline = Instant::now() + Duration::from_secs(secs);
    while !STOP_REQUESTED.load(Ordering::SeqCst) && Instant::now() < deadline {
        thread::sleep(Duration::from_secs(1));
    }
}

fn run_daemon() -> i32 {
    unsafe {
        signal(SIGTERM, on_stop as *const () as usize);
        signal(SIGINT, on_stop as *const () as usize);
        // Low-priority background scheduling so OTA checks never compete with boot/audio/AI
        nice(10);
    }
    // Clear any stale OTA error so boot clock screen is never hijacked
    set_error("ota", None, "", 120);
    let mut init_st = read_ota_status();
    write_ota_status(&mut init_st, false);

    // Non-blocking boot grace period: let Wi-Fi, DNS, NTP, and audio settle first
    sleep_interruptible(INITIAL_BOOT_DELAY_SECS);

    let mut consecutive_failures: u32 = 0;
    while !STOP_REQUESTED.load(Ordering::SeqCst) {
        let mut delay = 300u64;
        if let Ok(cfg) = load_config() {
            delay = cfg.interval_seconds.clamp(300, 86400);
            if cfg.enabled {
                if !wlan_is_connected() {
                    println!("[OTA] Wi-Fi not connected; deferring background check");
                    delay = (60u64 << consecutive_failures.min(3)).min(delay);
                    consecutive_failures = consecutive_failures.saturating_add(1);
                } else {
                    match acquire_update_lock().and_then(|_g| perform_check().map(|_| ())) {
                        Ok(()) => {
                            consecutive_failures = 0;
                        }
                        Err(e) => {
                            // Quietly log and back off; NEVER hijack LED screen or stall system
                            println!("[OTA] Background check deferred: {e}");
                            consecutive_failures = consecutive_failures.saturating_add(1);
                            let backoff = 120u64.saturating_mul(1u64 << consecutive_failures.min(4));
                            delay = backoff.min(delay);
                        }
                    }
                }
            }
        }
        sleep_interruptible(delay);
    }
    0
}

pub fn run(args: &[String]) -> i32 {
    let mut reboot_flag = false;
    let mut clean: Vec<&str> = Vec::new();
    for a in args {
        if a == "--reboot" {
            reboot_flag = true;
        } else {
            clean.push(a.as_str());
        }
    }
    let cmd = clean.first().copied().unwrap_or("status");
    if cmd == "ensure-boot-mounted" {
        if let Err(e) = ensure_boot_mounted() {
            eprintln!("[AtriOS-OTA] {e}");
            return 1;
        }
        return 0;
    }
    if cmd == "daemon" {
        return run_daemon();
    }
    if cmd == "status" {
        let Ok(cfg) = load_config() else { return 1; };
        let ota_st = read_ota_status_json();
        let j = obj(vec![
            ("version", Json::Str(current_version())),
            (
                "config",
                obj(vec![
                    ("repository", Json::Str(cfg.repository)),
                    ("enabled", Json::Bool(cfg.enabled)),
                    ("interval_seconds", Json::Num(cfg.interval_seconds as f64)),
                    ("public_key", Json::Str(cfg.public_key)),
                ]),
            ),
            ("ota", ota_st.clone()),
            ("last", ota_st),
        ]);
        println!("{}", j.to_string_pretty());
        return 0;
    }

    let res: Result<(), String> = (|| {
        let _lock = acquire_update_lock()?;
        match cmd {
            "check" => {
                let (st, maybe_rel) = perform_check()?;
                if let Some(r) = maybe_rel {
                    let status_label = if st.update_available { "available" } else { "up_to_date" };
                    println!("{}", release_to_json(&r, status_label, Path::new("")).to_string_pretty());
                } else {
                    println!("{}", st.to_json().to_string_pretty());
                }
            }
            "download" | "update" => check_and_download(cmd == "update", reboot_flag)?,
            "install" if clean.len() == 2 => {
                install_bundle_tar(Path::new(clean[1]), clean[1], reboot_flag, None)?;
            }
            "rollback" => {
                rollback_transaction()?;
                if reboot_flag {
                    unsafe {
                        sync();
                        reboot(RB_AUTOBOOT);
                    }
                }
            }
            "recover" => {
                if Path::new(JOURNAL).exists() {
                    if let Ok(j) = Json::parse(&fs::read_to_string(JOURNAL).unwrap_or_default()) {
                        if matches!(j.get_str("status"), Some("installing" | "rolling_back")) {
                            rollback_transaction()?;
                        }
                    }
                }
            }
            _ => {
                println!("Usage: atrios ota [status|check|download|update [--reboot]|install <tar> [--reboot]|rollback [--reboot]|recover|daemon]");
                return Err("Usage".into());
            }
        }
        Ok(())
    })();

    if let Err(e) = res {
        if e != "Usage" {
            // Never hijack LED screen on OTA errors; log quietly and persist in ota_status.json
            let mut st = read_ota_status();
            if st.state != "rolled_back" && st.state != "error" {
                st.state = "error".into();
                st.stage = "failed".into();
                st.message = e.clone();
                st.error = Some(e.clone());
                write_ota_status(&mut st, true);
            }
            eprintln!("[AtriOS-OTA] ERROR: {e}");
        }
        return 1;
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ota_status_json_contract_fields() {
        let mut st = OtaStatus::default_idle();
        st.state = "available".into();
        st.latest_version = Some("2.1.0".into());
        st.latest_tag = Some("v2.1.0".into());
        st.update_available = true;
        st.progress = 42;
        st.stage = "downloading".into();
        st.changelog = "Bug fixes".into();
        st.asset_size = 123456;
        st.downloaded_bytes = 50000;
        let j = st.to_json();
        for key in [
            "state",
            "current_version",
            "latest_version",
            "latest_tag",
            "update_available",
            "progress",
            "stage",
            "message",
            "error",
            "changelog",
            "asset_size",
            "downloaded_bytes",
            "rollback_available",
            "last_check_ts",
            "updated_ts",
            "reboot_required",
        ] {
            assert!(j.get(key).is_some(), "missing contracted key: {key}");
        }
        assert_eq!(j.get_str("state"), Some("available"));
        assert_eq!(j.get_str("latest_version"), Some("2.1.0"));
        assert_eq!(j.get("update_available").and_then(|v| v.as_bool()), Some(true));
        assert_eq!(j.get("progress").and_then(|v| v.as_i64()), Some(42));
    }

    #[test]
    fn transactional_install_auto_rollback_restores_originals_and_cleans_new_files() {
        let base = std::env::temp_dir().join(format!("atrios-ota-rb-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&base);
        let src_dir = base.join("src");
        let dst_dir = base.join("dst");
        let var_dir = base.join("var");
        fs::create_dir_all(&src_dir).unwrap();
        fs::create_dir_all(&dst_dir).unwrap();
        fs::create_dir_all(&var_dir).unwrap();

        let dst_a = dst_dir.join("atrios-runtime");
        let dst_b = dst_dir.join("Image");
        let dst_c = dst_dir.join("new-file.conf");
        fs::write(&dst_a, b"OLD_BINARY_V1").unwrap();
        fs::write(&dst_b, b"OLD_KERNEL_V1").unwrap();

        let src_a = src_dir.join("atrios-runtime");
        let src_b = src_dir.join("Image");
        let src_c = src_dir.join("new-file.conf");
        fs::write(&src_a, b"NEW_BINARY_V2").unwrap();
        fs::write(&src_b, b"NEW_KERNEL_V2").unwrap();
        fs::write(&src_c, b"NEW_CONFIG_V2").unwrap();

        let journal = var_dir.join("ota-transaction.json");
        let backup_base = var_dir.join("ota-rollback");
        let plan = vec![
            (src_a.clone(), dst_a.clone()),
            (src_b.clone(), dst_b.clone()),
            (src_c.clone(), dst_c.clone()),
        ];

        // Inject failure at index 2 after files 0 and 1 have already been overwritten
        let res = apply_plan_transactional(
            &plan,
            &journal,
            &backup_base,
            false,
            "2.0.0",
            "test-bundle",
            false,
            Some(2),
            None,
        );
        let err = res.expect_err("expected injected failure");
        assert!(err.contains("automatically rolled back"), "unexpected error: {err}");

        // Original files must be restored byte-for-byte, new file must not exist
        assert_eq!(fs::read(&dst_a).unwrap(), b"OLD_BINARY_V1");
        assert_eq!(fs::read(&dst_b).unwrap(), b"OLD_KERNEL_V1");
        assert!(!dst_c.exists(), "partial new file should not exist after rollback");

        // Now test clean install followed by explicit manual rollback
        apply_plan_transactional(
            &plan,
            &journal,
            &backup_base,
            false,
            "2.0.0",
            "test-bundle",
            false,
            None,
            None,
        )
        .expect("clean install should succeed");
        assert_eq!(fs::read(&dst_a).unwrap(), b"NEW_BINARY_V2");
        assert_eq!(fs::read(&dst_b).unwrap(), b"NEW_KERNEL_V2");
        assert_eq!(fs::read(&dst_c).unwrap(), b"NEW_CONFIG_V2");
        assert!(rollback_available_at(&journal));

        rollback_transaction_at(&journal, false).expect("manual rollback should succeed");
        assert_eq!(fs::read(&dst_a).unwrap(), b"OLD_BINARY_V1");
        assert_eq!(fs::read(&dst_b).unwrap(), b"OLD_KERNEL_V1");
        assert!(!dst_c.exists(), "newly installed file should be removed on manual rollback");

        let _ = fs::remove_dir_all(&base);
    }
}
