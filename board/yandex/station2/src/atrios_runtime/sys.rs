use std::ffi::CString;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::raw::{c_char, c_int, c_long, c_uint, c_ulong, c_void};
use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

pub const SIGINT: c_int = 2;
pub const SIGTERM: c_int = 15;
pub const SIGUSR1: c_int = 10;
pub const SIGUSR2: c_int = 12;

pub const LOCK_EX: c_int = 2;
pub const LOCK_NB: c_int = 4;

pub const AF_UNIX: c_int = 1;
pub const AF_INET: c_int = 2;
pub const AF_NETLINK: c_int = 16;
pub const AF_BLUETOOTH: c_int = 31;
pub const SOCK_STREAM: c_int = 1;
pub const SOCK_DGRAM: c_int = 2;
pub const SOCK_RAW: c_int = 3;
pub const SOL_SOCKET: c_int = 1;
pub const SO_REUSEADDR: c_int = 2;
pub const SO_RCVTIMEO: c_int = 20;
pub const SO_SNDTIMEO: c_int = 21;
pub const SHUT_RDWR: c_int = 2;

pub const BTPROTO_RFCOMM: c_int = 3;
pub const SOL_BLUETOOTH: c_int = 274;
pub const BT_SECURITY: c_int = 4;

pub const NETLINK_GENERIC: c_int = 16;
pub const SOL_NETLINK: c_int = 270;
pub const NETLINK_ADD_MEMBERSHIP: c_int = 1;

pub const TIOCSETD: c_ulong = 0x5423;
pub const HCIUARTSETPROTO: c_ulong = 0x4004_55c8;
pub const I2C_SLAVE_FORCE: c_ulong = 0x0706;
pub const SIOCGIFFLAGS: c_ulong = 0x8913;
pub const SIOCSIFFLAGS: c_ulong = 0x8914;
pub const WIFI_POWER_IOCTL: c_ulong = 0x6d03;

pub const TCSANOW: c_int = 0;
pub const TCIOFLUSH: c_int = 2;
pub const B115200: u32 = 0x1002;
pub const B3000000: u32 = 0x100d;
pub const CS8: u32 = 0o000060;
pub const CREAD: u32 = 0o000200;
pub const PARENB: u32 = 0o000400;
pub const PARODD: u32 = 0o001000;
pub const CLOCAL: u32 = 0o004000;
pub const CRTSCTS: u32 = 0x8000_0000;
pub const VTIME: usize = 5;
pub const VMIN: usize = 6;

pub const POLLIN: i16 = 0x0001;
pub const POLLOUT: i16 = 0x0004;

pub const CLOCK_REALTIME: c_int = 0;
pub const RB_AUTOBOOT: c_int = 0x0123_4567;
pub const RB_POWER_OFF: c_int = 0x4321_fedc_u32 as c_int;

pub const PROT_READ: c_int = 1;
pub const PROT_WRITE: c_int = 2;
pub const MAP_SHARED: c_int = 1;

pub const RTLD_NOW: c_int = 2;
pub const RTLD_GLOBAL: c_int = 0x100;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Timespec {
    pub tv_sec: i64,
    pub tv_nsec: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Timeval {
    pub tv_sec: i64,
    pub tv_usec: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PollFd {
    pub fd: c_int,
    pub events: i16,
    pub revents: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Termios {
    pub c_iflag: u32,
    pub c_oflag: u32,
    pub c_cflag: u32,
    pub c_lflag: u32,
    pub c_line: u8,
    pub c_cc: [u8; 32],
    pub c_ispeed: u32,
    pub c_ospeed: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SockAddrRc {
    pub rc_family: u16,
    pub rc_bdaddr: [u8; 6],
    pub rc_channel: u8,
    pub _pad: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SockAddrNl {
    pub nl_family: u16,
    pub nl_pad: u16,
    pub nl_pid: u32,
    pub nl_groups: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct InputEvent {
    pub sec: u64,
    pub usec: u64,
    pub ev_type: u16,
    pub code: u16,
    pub value: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Tm {
    pub tm_sec: c_int,
    pub tm_min: c_int,
    pub tm_hour: c_int,
    pub tm_mday: c_int,
    pub tm_mon: c_int,
    pub tm_year: c_int,
    pub tm_wday: c_int,
    pub tm_yday: c_int,
    pub tm_isdst: c_int,
    pub tm_gmtoff: c_long,
    pub tm_zone: *const c_char,
}

#[repr(C)]
pub struct StatVfs {
    pub f_bsize: c_ulong,
    pub f_frsize: c_ulong,
    pub f_blocks: u64,
    pub f_bfree: u64,
    pub f_bavail: u64,
    pub f_files: u64,
    pub f_ffree: u64,
    pub f_favail: u64,
    pub f_fsid: c_ulong,
    pub f_flag: c_ulong,
    pub f_namemax: c_ulong,
    pub f_spare: [c_int; 6],
}

#[repr(C)]
pub struct UtsName {
    pub sysname: [u8; 65],
    pub nodename: [u8; 65],
    pub release: [u8; 65],
    pub version: [u8; 65],
    pub machine: [u8; 65],
    pub domainname: [u8; 65],
}

extern "C" {
    pub fn signal(signum: c_int, handler: usize) -> usize;
    pub fn kill(pid: c_int, sig: c_int) -> c_int;
    pub fn geteuid() -> u32;
    pub fn flock(fd: c_int, op: c_int) -> c_int;
    pub fn ioctl(fd: c_int, request: c_ulong, ...) -> c_int;
    pub fn fcntl(fd: c_int, cmd: c_int, ...) -> c_int;
    pub fn umask(mask: u32) -> u32;
    pub fn mknod(pathname: *const c_char, mode: u32, dev: u64) -> c_int;
    pub fn sync();
    pub fn reboot(cmd: c_int) -> c_int;
    pub fn clock_settime(clk_id: c_int, tp: *const Timespec) -> c_int;
    pub fn tzset();
    pub fn localtime_r(timep: *const i64, result: *mut Tm) -> *mut Tm;
    pub fn gmtime_r(timep: *const i64, result: *mut Tm) -> *mut Tm;
    pub fn strftime(s: *mut c_char, max: usize, format: *const c_char, tm: *const Tm) -> usize;
    pub fn statvfs(path: *const c_char, buf: *mut StatVfs) -> c_int;
    pub fn uname(buf: *mut UtsName) -> c_int;
    pub fn poll(fds: *mut PollFd, nfds: c_ulong, timeout: c_int) -> c_int;
    pub fn mmap(
        addr: *mut c_void,
        length: usize,
        prot: c_int,
        flags: c_int,
        fd: c_int,
        offset: i64,
    ) -> *mut c_void;
    pub fn munmap(addr: *mut c_void, length: usize) -> c_int;
    pub fn tcgetattr(fd: c_int, termios_p: *mut Termios) -> c_int;
    pub fn tcsetattr(fd: c_int, optional_actions: c_int, termios_p: *const Termios) -> c_int;
    pub fn tcflush(fd: c_int, queue_selector: c_int) -> c_int;
    pub fn cfmakeraw(termios_p: *mut Termios);
    pub fn posix_openpt(flags: c_int) -> c_int;
    pub fn grantpt(fd: c_int) -> c_int;
    pub fn unlockpt(fd: c_int) -> c_int;
    pub fn ptsname_r(fd: c_int, buf: *mut c_char, buflen: usize) -> c_int;
    pub fn socket(domain: c_int, ty: c_int, protocol: c_int) -> c_int;
    pub fn bind(sockfd: c_int, addr: *const c_void, addrlen: u32) -> c_int;
    pub fn listen(sockfd: c_int, backlog: c_int) -> c_int;
    pub fn accept(sockfd: c_int, addr: *mut c_void, addrlen: *mut u32) -> c_int;
    pub fn connect(sockfd: c_int, addr: *const c_void, addrlen: u32) -> c_int;
    pub fn setsockopt(
        sockfd: c_int,
        level: c_int,
        optname: c_int,
        optval: *const c_void,
        optlen: u32,
    ) -> c_int;
    pub fn shutdown(sockfd: c_int, how: c_int) -> c_int;
    pub fn recv(sockfd: c_int, buf: *mut c_void, len: usize, flags: c_int) -> isize;
    pub fn send(sockfd: c_int, buf: *const c_void, len: usize, flags: c_int) -> isize;
    pub fn read(fd: c_int, buf: *mut c_void, count: usize) -> isize;
    pub fn write(fd: c_int, buf: *const c_void, count: usize) -> isize;
    pub fn close(fd: c_int) -> c_int;
    pub fn dlopen(filename: *const c_char, flags: c_int) -> *mut c_void;
    pub fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    pub fn if_nametoindex(ifname: *const c_char) -> c_uint;
}

pub fn recv_or_read(fd: c_int, buf: &mut [u8]) -> isize {
    unsafe { read(fd, buf.as_mut_ptr() as *mut c_void, buf.len()) }
}

pub fn write_fd(fd: c_int, buf: &[u8]) -> isize {
    unsafe { write(fd, buf.as_ptr() as *const c_void, buf.len()) }
}

pub fn makedev(maj: u32, min: u32) -> u64 {
    ((maj as u64 & 0xffff_f000) << 32)
        | ((maj as u64 & 0x0000_0fff) << 8)
        | ((min as u64 & 0xffff_ff00) << 12)
        | (min as u64 & 0x0000_00ff)
}

pub fn now_unix_secs() -> f64 {
    match SystemTime::now().duration_since(UNIX_EPOCH) {
        Ok(d) => d.as_secs_f64(),
        Err(_) => 0.0,
    }
}

pub fn now_unix_i64() -> i64 {
    match SystemTime::now().duration_since(UNIX_EPOCH) {
        Ok(d) => d.as_secs() as i64,
        Err(_) => 0,
    }
}

pub fn format_local(fmt: &str, ts: Option<i64>) -> String {
    let t = ts.unwrap_or_else(now_unix_i64);
    let mut tm = unsafe { std::mem::zeroed::<Tm>() };
    unsafe {
        tzset();
        if localtime_r(&t, &mut tm).is_null() {
            return String::new();
        }
    }
    let c_fmt = CString::new(fmt).unwrap_or_default();
    let mut buf = [0u8; 128];
    let n = unsafe {
        strftime(
            buf.as_mut_ptr() as *mut c_char,
            buf.len(),
            c_fmt.as_ptr(),
            &tm,
        )
    };
    String::from_utf8_lossy(&buf[..n]).into_owned()
}

pub fn format_utc(fmt: &str, ts: i64) -> String {
    let mut tm = unsafe { std::mem::zeroed::<Tm>() };
    unsafe {
        if gmtime_r(&ts, &mut tm).is_null() {
            return String::new();
        }
    }
    let c_fmt = CString::new(fmt).unwrap_or_default();
    let mut buf = [0u8; 128];
    let n = unsafe {
        strftime(
            buf.as_mut_ptr() as *mut c_char,
            buf.len(),
            c_fmt.as_ptr(),
            &tm,
        )
    };
    String::from_utf8_lossy(&buf[..n]).into_owned()
}

pub fn uname_strings() -> (String, String) {
    let mut u = unsafe { std::mem::zeroed::<UtsName>() };
    if unsafe { uname(&mut u) } != 0 {
        return ("Linux".into(), "aarch64".into());
    }
    let to_str = |arr: &[u8; 65]| -> String {
        let len = arr.iter().position(|&b| b == 0).unwrap_or(arr.len());
        String::from_utf8_lossy(&arr[..len]).into_owned()
    };
    (to_str(&u.release), to_str(&u.machine))
}

pub fn free_bytes(path: &Path) -> io::Result<u64> {
    let c_path = CString::new(path.to_string_lossy().as_bytes())
        .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "NUL in path"))?;
    let mut sv = unsafe { std::mem::zeroed::<StatVfs>() };
    if unsafe { statvfs(c_path.as_ptr(), &mut sv) } != 0 {
        return Err(io::Error::last_os_error());
    }
    let block = if sv.f_frsize > 0 { sv.f_frsize } else { sv.f_bsize };
    Ok(sv.f_bavail.saturating_mul(block))
}

pub fn sync_dir(path: &Path) -> io::Result<()> {
    if let Ok(f) = File::open(path) {
        if let Err(e) = f.sync_all() {
            let raw = e.raw_os_error().unwrap_or(0);
            if raw != libc_einval() && raw != libc_enotsup() {
                return Err(e);
            }
        }
    }
    Ok(())
}

fn libc_einval() -> i32 {
    22
}
fn libc_enotsup() -> i32 {
    95
}

pub fn atomic_write_bytes(path: &Path, data: &[u8], mode: u32, do_fsync: bool) -> io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let parent = path.parent().unwrap_or_else(|| Path::new("."));
    let base = path.file_name().and_then(|s| s.to_str()).unwrap_or("tmp");
    let pid = std::process::id();
    let mut attempt = 0u32;
    let (mut file, tmp_path) = loop {
        let cand = parent.join(format!(".{base}.{pid}.{attempt}.tmp"));
        match OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(mode)
            .open(&cand)
        {
            Ok(f) => break (f, cand),
            Err(e) if e.kind() == io::ErrorKind::AlreadyExists && attempt < 16 => {
                attempt += 1;
            }
            Err(e) => return Err(e),
        }
    };
    let res = (|| -> io::Result<()> {
        file.set_permissions(fs::Permissions::from_mode(mode))?;
        file.write_all(data)?;
        if do_fsync {
            file.flush()?;
            file.sync_all()?;
        }
        drop(file);
        fs::rename(&tmp_path, path)?;
        if do_fsync {
            sync_dir(parent)?;
        }
        Ok(())
    })();
    if res.is_err() {
        let _ = fs::remove_file(&tmp_path);
    }
    res
}

pub fn find_fb(keywords: &[&str], fallback: &[&str]) -> String {
    if let Ok(entries) = fs::read_dir("/sys/class/graphics") {
        let mut names: Vec<PathBuf> = entries
            .filter_map(|e| e.ok().map(|x| x.path()))
            .filter(|p| {
                p.file_name()
                    .and_then(|s| s.to_str())
                    .map(|n| n.starts_with("fb"))
                    .unwrap_or(false)
            })
            .collect();
        names.sort();
        for dir in names {
            if let Ok(fb_name) = fs::read_to_string(dir.join("name")) {
                let low = fb_name.trim().to_ascii_lowercase();
                if keywords.iter().any(|k| low.contains(&k.to_ascii_lowercase())) {
                    if let Some(dev_name) = dir.file_name().and_then(|s| s.to_str()) {
                        let p = format!("/dev/{dev_name}");
                        if Path::new(&p).exists() {
                            return p;
                        }
                    }
                }
            }
        }
    }
    for cand in fallback {
        if Path::new(cand).exists() {
            return (*cand).to_string();
        }
    }
    fallback.first().copied().unwrap_or("/dev/fb0").to_string()
}

pub fn find_lux_sensor() -> Option<String> {
    let mut dirs: Vec<PathBuf> = fs::read_dir("/sys/bus/iio/devices")
        .ok()?
        .filter_map(|e| e.ok().map(|x| x.path()))
        .collect();
    dirs.sort();
    for dir in dirs {
        if let Ok(files) = fs::read_dir(&dir) {
            let mut fnames: Vec<PathBuf> = files.filter_map(|e| e.ok().map(|x| x.path())).collect();
            fnames.sort();
            for p in fnames {
                if let Some(n) = p.file_name().and_then(|s| s.to_str()) {
                    if n.starts_with("in_illuminance") || n.starts_with("in_intensity") {
                        return Some(p.to_string_lossy().into_owned());
                    }
                }
            }
        }
    }
    None
}

pub fn run_quiet(prog: &str, args: &[&str]) -> bool {
    Command::new(prog)
        .args(args)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

pub fn pkill_pattern(pattern: &str) {
    let _ = run_quiet("pkill", &["-f", pattern]);
}

pub fn sha256_bytes(data: &[u8]) -> [u8; 32] {
    let mut h = Sha256::new();
    h.update(data);
    h.finalize()
}

pub fn sha256_file_hex(path: &Path) -> io::Result<String> {
    let mut f = File::open(path)?;
    let mut h = Sha256::new();
    let mut buf = [0u8; 65536];
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(hex_lower(&h.finalize()))
}

pub fn hex_lower(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        out.push(HEX[(b >> 4) as usize] as char);
        out.push(HEX[(b & 0xf) as usize] as char);
    }
    out
}

pub fn hex_upper(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    let mut out = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        out.push(HEX[(b >> 4) as usize] as char);
        out.push(HEX[(b & 0xf) as usize] as char);
    }
    out
}

pub struct Sha256 {
    state: [u32; 8],
    len_bits: u64,
    buf: [u8; 64],
    buf_len: usize,
}

impl Sha256 {
    pub fn new() -> Self {
        Self {
            state: [
                0x6a09_e667, 0xbb67_ae85, 0x3c6e_f372, 0xa54f_f53a,
                0x510e_527f, 0x9b05_688c, 0x1f83_d9ab, 0x5be0_cd19,
            ],
            len_bits: 0,
            buf: [0u8; 64],
            buf_len: 0,
        }
    }

    pub fn update(&mut self, mut data: &[u8]) {
        self.len_bits = self.len_bits.wrapping_add((data.len() as u64).wrapping_mul(8));
        if self.buf_len > 0 {
            let take = (64 - self.buf_len).min(data.len());
            self.buf[self.buf_len..self.buf_len + take].copy_from_slice(&data[..take]);
            self.buf_len += take;
            data = &data[take..];
            if self.buf_len == 64 {
                let block = self.buf;
                self.compress(&block);
                self.buf_len = 0;
            }
        }
        while data.len() >= 64 {
            let mut block = [0u8; 64];
            block.copy_from_slice(&data[..64]);
            self.compress(&block);
            data = &data[64..];
        }
        if !data.is_empty() {
            self.buf[..data.len()].copy_from_slice(data);
            self.buf_len = data.len();
        }
    }

    pub fn finalize(mut self) -> [u8; 32] {
        self.buf[self.buf_len] = 0x80;
        self.buf_len += 1;
        if self.buf_len > 56 {
            for b in &mut self.buf[self.buf_len..64] {
                *b = 0;
            }
            let block = self.buf;
            self.compress(&block);
            self.buf_len = 0;
        }
        for b in &mut self.buf[self.buf_len..56] {
            *b = 0;
        }
        self.buf[56..64].copy_from_slice(&self.len_bits.to_be_bytes());
        let block = self.buf;
        self.compress(&block);
        let mut out = [0u8; 32];
        for (i, w) in self.state.iter().enumerate() {
            out[i * 4..(i + 1) * 4].copy_from_slice(&w.to_be_bytes());
        }
        out
    }

    fn compress(&mut self, block: &[u8; 64]) {
        const K: [u32; 64] = [
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        ];
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([block[i * 4], block[i * 4 + 1], block[i * 4 + 2], block[i * 4 + 3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = self.state;
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let t1 = h.wrapping_add(s1).wrapping_add(ch).wrapping_add(K[i]).wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        for (slot, delta) in self.state.iter_mut().zip([a, b, c, d, e, f, g, h]) {
            *slot = slot.wrapping_add(delta);
        }
    }
}
