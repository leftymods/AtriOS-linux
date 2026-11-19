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
use crate::status::{error_kind_from_msg, set_error};
use crate::sys::{
    atomic_write_bytes, flock, free_bytes, hex_lower, reboot, sha256_file_hex, signal, sync,
    sync_dir, uname_strings, Sha256, LOCK_EX, LOCK_NB, RB_AUTOBOOT, SIGINT, SIGTERM,
};

const CONFIG_FILE: &str = "/etc/atrios/updates.json";
const STATE_FILE: &str = "/var/lib/atrios/ota_status.json";
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
        set_error("ota", Some("signature"), "Invalid OTA signature", 120);
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

fn rollback_transaction() -> Result<(), String> {
    if !Path::new(JOURNAL).exists() {
        return Err("No OTA transaction available for rollback".into());
    }
    let txt = fs::read_to_string(JOURNAL).map_err(|e| e.to_string())?;
    let mut state = Json::parse(&txt)?;
    if state.get("boot").and_then(|v| v.as_bool()) == Some(true) {
        ensure_boot_mounted()?;
    }
    if let Json::Obj(ref mut m) = state {
        m.insert("status".into(), Json::Str("rolling_back".into()));
    }
    atomic_json(Path::new(JOURNAL), &state)?;

    let backup_dir = PathBuf::from(state.get_str("backup_dir").ok_or("Missing backup_dir")?);
    if let Some(entries) = state.get("entries").and_then(|v| v.as_arr()) {
        for entry in entries.iter().rev() {
            let dest = PathBuf::from(entry.get_str("destination").unwrap_or(""));
            let existed = entry.get("existed").and_then(|v| v.as_bool()).unwrap_or(false);
            let backup = entry.get_str("backup").unwrap_or("");
            if existed {
                atomic_copy(&backup_dir.join(backup), &dest)?;
            } else if dest.exists() {
                fs::remove_file(&dest).map_err(|e| e.to_string())?;
                if let Some(p) = dest.parent() {
                    let _ = sync_dir(p);
                }
            }
        }
    }
    let prev = state.get("previous").cloned();
    if let Json::Obj(ref mut m) = state {
        m.insert("status".into(), Json::Str("rolled_back".into()));
    }
    let to_write = match prev {
        Some(p) if !matches!(p, Json::Null) => p,
        _ => state.clone(),
    };
    atomic_json(Path::new(JOURNAL), &to_write)?;
    atomic_json(
        Path::new(STATE_FILE),
        &obj(vec![
            ("status", Json::Str("rolled_back".into())),
            ("version", Json::Str(current_version())),
        ]),
    )?;
    Ok(())
}

fn install_bundle_tar(
    pkg_path: &Path,
    source_label: &str,
    do_reboot: bool,
    expected_version: Option<&str>,
) -> Result<(), String> {
    let cfg = load_config()?;
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
        if payloads.contains_key("kernel") || payloads.contains_key("dtb") {
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

        let previous = if Path::new(JOURNAL).exists() {
            Json::parse(&fs::read_to_string(JOURNAL).unwrap_or_default()).ok()
        } else {
            None
        };
        let tx_backup = PathBuf::from(format!("{BACKUP_DIR}-{}", std::process::id()));
        fs::create_dir_all(&tx_backup).map_err(|e| e.to_string())?;

        let mut entries_json: Vec<Json> = Vec::new();
        let build_state = |status_str: &str, ents: &[Json], prev: Option<Json>| {
            let mut m = BTreeMap::new();
            m.insert("status".into(), Json::Str(status_str.into()));
            if let Some(p) = prev {
                m.insert("previous".into(), p);
            }
            m.insert("backup_dir".into(), Json::Str(tx_backup.to_string_lossy().into_owned()));
            m.insert("version".into(), Json::Str(m_ver.into()));
            m.insert("source".into(), Json::Str(source_label.into()));
            m.insert("boot".into(), Json::Bool(payloads.contains_key("kernel") || payloads.contains_key("dtb")));
            m.insert("entries".into(), Json::Arr(ents.to_vec()));
            Json::Obj(m)
        };

        atomic_json(Path::new(JOURNAL), &build_state("installing", &entries_json, previous.clone()))?;
        let tx_res: Result<(), String> = (|| {
            for (idx, (src, dst)) in plan.iter().enumerate() {
                let existed = dst.exists();
                let bname = idx.to_string();
                if existed {
                    atomic_copy(dst, &tx_backup.join(&bname))?;
                }
                entries_json.push(obj(vec![
                    ("destination", Json::Str(dst.to_string_lossy().into_owned())),
                    ("existed", Json::Bool(existed)),
                    ("backup", Json::Str(bname)),
                ]));
                atomic_json(Path::new(JOURNAL), &build_state("installing", &entries_json, previous.clone()))?;
                atomic_copy(src, dst)?;
            }
            atomic_json(Path::new(JOURNAL), &build_state("installed", &entries_json, None))?;
            atomic_json(
                Path::new(STATE_FILE),
                &obj(vec![
                    ("version", Json::Str(m_ver.into())),
                    ("status", Json::Str("installed".into())),
                    ("source", Json::Str(source_label.into())),
                ]),
            )?;
            set_error("ota", None, "", 120);
            if let Some(ref p) = previous {
                if let Some(old) = p.get_str("backup_dir") {
                    let op = PathBuf::from(old);
                    if op.parent() == Some(bparent) {
                        let _ = fs::remove_dir_all(op);
                    }
                }
            }
            Ok(())
        })();
        if tx_res.is_err() {
            let _ = rollback_transaction();
        }
        tx_res
    })();

    let _ = fs::remove_dir_all(&tmp_dir);
    install_res?;
    if do_reboot {
        unsafe {
            sync();
            reboot(RB_AUTOBOOT);
        }
    }
    Ok(())
}

struct ReleaseInfo {
    tag: String,
    version: String,
    asset: String,
    url: String,
    size: u64,
    sha256: String,
}

fn release_to_json(r: &ReleaseInfo, status: &str, path: &Path) -> Json {
    obj(vec![
        ("tag", Json::Str(r.tag.clone())),
        ("version", Json::Str(r.version.clone())),
        ("asset", Json::Str(r.asset.clone())),
        ("url", Json::Str(r.url.clone())),
        ("size", Json::Num(r.size as f64)),
        ("sha256", Json::Str(r.sha256.clone())),
        ("status", Json::Str(status.into())),
        ("path", Json::Str(path.to_string_lossy().into_owned())),
    ])
}

fn latest_release(cfg: &OtaConfig) -> Result<ReleaseInfo, String> {
    let hdrs = [("X-GitHub-Api-Version", "2022-11-28")];
    let latest_url = format!("https://api.github.com/repos/{}/releases/latest", cfg.repository);
    let mut data = request_json(&latest_url, None, &hdrs, None, 25, 1024 * 1024, None)?;
    let board = device_id()?;
    let asset_name = format!("AtriOS-{board}-ota.tar.gz");

    if data.get_str("tag_name").unwrap_or("").starts_with("app-") {
        let list_url = format!("https://api.github.com/repos/{}/releases?per_page=100", cfg.repository);
        let raw = request_bytes(&list_url, None, &hdrs, None, 25, 4 * 1024 * 1024, None)?;
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
        let body = request_bytes(&side_url, None, &[], None, 25, 1024, None)?;
        let text = String::from_utf8_lossy(&body);
        let tok = text.split_whitespace().next().unwrap_or("");
        if tok.len() != 64 || !tok.bytes().all(|b| b.is_ascii_hexdigit()) {
            return Err("Invalid release checksum file".into());
        }
        tok.to_ascii_lowercase()
    };
    let ver = tag.strip_prefix('v').unwrap_or(&tag).to_string();
    Ok(ReleaseInfo { tag, version: ver, asset: asset_name, url, size, sha256: sha })
}

fn download_release(rel: &ReleaseInfo) -> Result<PathBuf, String> {
    fs::create_dir_all(CACHE_DIR).map_err(|e| e.to_string())?;
    let target = Path::new(CACHE_DIR).join(format!("{}-{}", rel.tag, rel.asset));
    if target.is_file() && sha256_file_hex(&target).ok().as_deref() == Some(&rel.sha256) {
        return Ok(target);
    }
    if free_bytes(Path::new(CACHE_DIR)).map_err(|e| e.to_string())? < rel.size + 16 * 1024 * 1024 {
        return Err("ENOSPC: Insufficient update download storage".into());
    }
    let tmp = Path::new(CACHE_DIR).join(format!(".download-{}.tmp", std::process::id()));
    let res: Result<PathBuf, String> = (|| {
        let mut out = File::create(&tmp).map_err(|e| e.to_string())?;
        let mut hasher = Sha256::new();
        let mut got_size = 0u64;
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
                Ok(())
            },
        )?;
        out.flush().map_err(|e| e.to_string())?;
        out.sync_all().map_err(|e| e.to_string())?;
        let digest = hex_lower(&hasher.finalize());
        if got_size != rel.size || digest != rel.sha256 {
            set_error("ota", Some("signature"), "Release download SHA-256/size mismatch", 120);
            return Err("Release download SHA-256/size mismatch".into());
        }
        fs::rename(&tmp, &target).map_err(|e| e.to_string())?;
        let _ = sync_dir(Path::new(CACHE_DIR));
        atomic_json(Path::new(STATE_FILE), &release_to_json(rel, "downloaded", &target))?;
        set_error("ota", None, "", 120);
        Ok(target.clone())
    })();
    if res.is_err() {
        let _ = fs::remove_file(&tmp);
    }
    res
}

fn check_and_download(install: bool, do_reboot: bool) -> Result<(), String> {
    let cfg = load_config()?;
    let rel = latest_release(&cfg)?;
    if version_key(&rel.version)? <= version_key(&current_version())? {
        println!("AtriOS is up to date");
        set_error("ota", None, "", 120);
        return Ok(());
    }
    let path = download_release(&rel)?;
    println!("Downloaded: {}", path.display());
    if install {
        install_bundle_tar(&path, &rel.url, do_reboot, Some(&rel.version))?;
    }
    Ok(())
}

fn run_daemon() -> i32 {
    unsafe {
        signal(SIGTERM, on_stop as *const () as usize);
        signal(SIGINT, on_stop as *const () as usize);
    }
    while !STOP_REQUESTED.load(Ordering::SeqCst) {
        let mut delay = 60u64;
        if let Ok(cfg) = load_config() {
            delay = cfg.interval_seconds.clamp(300, 86400);
            if cfg.enabled {
                let res = acquire_update_lock().and_then(|_g| check_and_download(false, false));
                if let Err(e) = res {
                    if e.contains("No stable firmware release") {
                        set_error("ota", None, "", 120);
                        delay = 21600;
                    } else if !e.contains("HTTP Error 404") {
                        set_error("ota", Some(error_kind_from_msg(&e, "update")), &e, 120);
                    }
                    println!("[OTA] {e}");
                }
            }
        }
        let deadline = Instant::now() + Duration::from_secs(delay);
        while !STOP_REQUESTED.load(Ordering::SeqCst) && Instant::now() < deadline {
            thread::sleep(Duration::from_secs(1));
        }
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
        let last = fs::read_to_string(STATE_FILE)
            .ok()
            .and_then(|s| Json::parse(&s).ok())
            .unwrap_or(Json::Null);
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
            ("last", last),
        ]);
        println!("{}", j.to_string_pretty());
        return 0;
    }

    let res: Result<(), String> = (|| {
        let _lock = acquire_update_lock()?;
        match cmd {
            "check" => {
                let cfg = load_config()?;
                let r = latest_release(&cfg)?;
                println!("{}", release_to_json(&r, "available", Path::new("")).to_string_pretty());
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
                println!("Usage: atrios ota [status|check|download|update [--reboot]|install <tar> [--reboot]|rollback|recover|daemon]");
                return Err("Usage".into());
            }
        }
        Ok(())
    })();

    if let Err(e) = res {
        if e != "Usage" {
            set_error("ota", Some(error_kind_from_msg(&e, "update")), &e, 120);
            eprintln!("[AtriOS-OTA] ERROR: {e}");
        }
        return 1;
    }
    0
}
