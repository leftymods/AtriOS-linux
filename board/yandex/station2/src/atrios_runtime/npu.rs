use std::ffi::{CStr, CString};
use std::fs::OpenOptions;
use std::os::raw::{c_char, c_int, c_ulong, c_void};
use std::os::unix::io::AsRawFd;
use std::path::Path;
use std::sync::OnceLock;

use crate::sys::{dlopen, dlsym, RTLD_GLOBAL, RTLD_NOW};

extern "C" {
    fn ioctl(fd: c_int, request: c_ulong, ...) -> c_int;
}

// Linux DRM ioctl numbers for aarch64
// _IOWR('d', 0x00, struct drm_version) -> sizeof(drm_version) on 64-bit = 64
const DRM_IOCTL_VERSION: c_ulong = 0xC040_6400;
// _IOWR('d', 0x40 + 0x00, struct drm_etnaviv_param) -> sizeof = 16
const DRM_IOCTL_ETNAVIV_GET_PARAM: c_ulong = 0xC010_6440;

const ETNAVIV_PARAM_GPU_MODEL: u32 = 0x01;
const ETNAVIV_PARAM_GPU_REVISION: u32 = 0x02;
const ETNAVIV_PARAM_GPU_NN_CORE_COUNT: u32 = 0x1e;
const ETNAVIV_PARAM_GPU_NN_MAD_PER_CORE: u32 = 0x1f;
const ETNAVIV_PARAM_GPU_TP_CORE_COUNT: u32 = 0x20;
const ETNAVIV_PARAM_GPU_ON_CHIP_SRAM_SIZE: u32 = 0x21;

#[repr(C)]
struct DrmVersion {
    version_major: c_int,
    version_minor: c_int,
    version_patchlevel: c_int,
    name_len: usize,
    name: *mut c_char,
    date_len: usize,
    date: *mut c_char,
    desc_len: usize,
    desc: *mut c_char,
}

#[repr(C)]
struct DrmEtnavivParam {
    pipe: u32,
    param: u32,
    value: u64,
}

#[derive(Clone, Debug)]
pub struct NpuInfo {
    pub device_path: String,
    pub driver: String,
    pub hw_ready: bool,
    pub model: u32,
    pub revision: u32,
    pub nn_cores: u32,
    pub nn_mad_per_core: u32,
    pub sram_bytes: u64,
    pub teflon_loaded: bool,
    pub tflite_loaded: bool,
}

static NPU_INFO: OnceLock<NpuInfo> = OnceLock::new();

const TEFLON_PATHS: &[&str] = &[
    "/usr/lib/atrios/libteflon.so",
    "/usr/lib/libteflon.so",
    "/usr/lib/aarch64-linux-gnu/libteflon.so",
];

const TFLITE_PATHS: &[&str] = &[
    "/usr/lib/atrios/libtensorflowlite_c.so",
    "/usr/lib/libtensorflowlite_c.so",
    "/usr/lib/aarch64-linux-gnu/libtensorflowlite_c.so",
];

fn etnaviv_get_param(fd: c_int, param: u32) -> Option<u64> {
    let mut req = DrmEtnavivParam {
        pipe: 0,
        param,
        value: 0,
    };
    let rc = unsafe { ioctl(fd, DRM_IOCTL_ETNAVIV_GET_PARAM, &mut req) };
    if rc == 0 {
        Some(req.value)
    } else {
        None
    }
}

fn probe_render_node(path: &str) -> Option<NpuInfo> {
    let f = OpenOptions::new().read(true).write(true).open(path).ok()?;
    let fd = f.as_raw_fd();
    let mut name_buf = [0u8; 64];
    let mut date_buf = [0u8; 64];
    let mut desc_buf = [0u8; 128];
    let mut ver = DrmVersion {
        version_major: 0,
        version_minor: 0,
        version_patchlevel: 0,
        name_len: name_buf.len() - 1,
        name: name_buf.as_mut_ptr() as *mut c_char,
        date_len: date_buf.len() - 1,
        date: date_buf.as_mut_ptr() as *mut c_char,
        desc_len: desc_buf.len() - 1,
        desc: desc_buf.as_mut_ptr() as *mut c_char,
    };
    let rc = unsafe { ioctl(fd, DRM_IOCTL_VERSION, &mut ver) };
    if rc != 0 {
        return None;
    }
    let driver = unsafe { CStr::from_ptr(name_buf.as_ptr() as *const c_char) }
        .to_string_lossy()
        .into_owned();
    if driver != "etnaviv" {
        return None;
    }
    let model = etnaviv_get_param(fd, ETNAVIV_PARAM_GPU_MODEL).unwrap_or(0) as u32;
    let revision = etnaviv_get_param(fd, ETNAVIV_PARAM_GPU_REVISION).unwrap_or(0) as u32;
    let nn_cores = etnaviv_get_param(fd, ETNAVIV_PARAM_GPU_NN_CORE_COUNT).unwrap_or(0) as u32;
    let nn_mad_per_core = etnaviv_get_param(fd, ETNAVIV_PARAM_GPU_NN_MAD_PER_CORE).unwrap_or(0) as u32;
    let _tp_cores = etnaviv_get_param(fd, ETNAVIV_PARAM_GPU_TP_CORE_COUNT).unwrap_or(0);
    let sram_bytes = etnaviv_get_param(fd, ETNAVIV_PARAM_GPU_ON_CHIP_SRAM_SIZE).unwrap_or(0);

    let teflon_loaded = probe_dl(TEFLON_PATHS, "tflite_plugin_create_delegate");
    let tflite_loaded = probe_dl(TFLITE_PATHS, "TfLiteModelCreateFromFile");

    Some(NpuInfo {
        device_path: path.to_string(),
        driver,
        hw_ready: true,
        model,
        revision,
        nn_cores,
        nn_mad_per_core,
        sram_bytes,
        teflon_loaded,
        tflite_loaded,
    })
}

fn probe_dl(paths: &[&str], sym_name: &str) -> bool {
    let Ok(csym) = CString::new(sym_name) else {
        return false;
    };
    for &p in paths {
        if !Path::new(p).exists() {
            continue;
        }
        if let Ok(cp) = CString::new(p) {
            unsafe {
                let h = dlopen(cp.as_ptr(), RTLD_NOW | RTLD_GLOBAL);
                if !h.is_null() {
                    let s: *mut c_void = dlsym(h, csym.as_ptr());
                    if !s.is_null() {
                        return true;
                    }
                }
            }
        }
    }
    false
}

fn detect() -> NpuInfo {
    for p in ["/dev/dri/renderD128", "/dev/dri/renderD129", "/dev/dri/card1", "/dev/dri/card0"] {
        if Path::new(p).exists() {
            if let Some(info) = probe_render_node(p) {
                return info;
            }
        }
    }
    NpuInfo {
        device_path: String::new(),
        driver: String::new(),
        hw_ready: false,
        model: 0,
        revision: 0,
        nn_cores: 0,
        nn_mad_per_core: 0,
        sram_bytes: 0,
        teflon_loaded: false,
        tflite_loaded: false,
    }
}

pub fn init() -> &'static NpuInfo {
    let info = NPU_INFO.get_or_init(detect);
    println!("[npu] {}", status_line());
    info
}

pub fn info() -> &'static NpuInfo {
    NPU_INFO.get_or_init(detect)
}

pub fn is_hardware_ready() -> bool {
    info().hw_ready
}

pub fn status_line() -> String {
    let i = info();
    if !i.hw_ready {
        return "offline (no etnaviv /dev/dri/renderD128)".into();
    }
    format!(
        "ready dev={} driver={} model=0x{:x} rev=0x{:x} nn_cores={} mad={} sram={}KB teflon={} tflite={}",
        i.device_path,
        i.driver,
        i.model,
        i.revision,
        i.nn_cores,
        i.nn_mad_per_core,
        i.sram_bytes / 1024,
        i.teflon_loaded,
        i.tflite_loaded
    )
}
