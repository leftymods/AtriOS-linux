use std::fs;
use std::path::Path;

use crate::json::{obj, Json};

pub const HARDWARE_MANIFEST_PATH: &str = "/etc/atrios/hardware.json";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HardwareManifest {
    pub schema: u32,
    pub board: String,
    pub name: String,
    pub vendor: String,
    pub soc: String,
    pub dtb: String,
    pub display_type: String,
    pub display_width: u16,
    pub display_height: u16,
    pub ring_leds: u16,
    pub amp_i2c_bus: u8,
    pub amp_tweeter_addr: u16,
    pub amp_woofer_addr: u16,
    pub mic_channels: u8,
    pub capture_device: String,
    pub bt_uart: String,
    pub zigbee_uart: String,
    pub has_rotary: bool,
    pub has_als: bool,
    pub has_zigbee: bool,
    pub has_bt: bool,
}

impl Default for HardwareManifest {
    /// Safe fallback profile for Yandex Station Max (Amlogic SM1 / S905X3).
    fn default() -> Self {
        Self {
            schema: 1,
            board: "station-max".into(),
            name: "Yandex Station Max".into(),
            vendor: "yandex".into(),
            soc: "sm1".into(),
            dtb: "meson-sm1-yandex-station-max.dtb".into(),
            display_type: "matrix-fb".into(),
            display_width: 25,
            display_height: 16,
            ring_leds: 24,
            amp_i2c_bus: 2,
            amp_tweeter_addr: 0x2a,
            amp_woofer_addr: 0x2b,
            mic_channels: 8,
            capture_device: "hw:0,3".into(),
            bt_uart: "/dev/ttyAML1".into(),
            zigbee_uart: "/dev/ttyAML2".into(),
            has_rotary: true,
            has_als: true,
            has_zigbee: true,
            has_bt: true,
        }
    }
}

pub fn parse_i2c_addr(raw: &str) -> Option<u16> {
    let s = raw.trim();
    if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        u16::from_str_radix(hex, 16).ok()
    } else {
        s.parse::<u16>().ok()
    }
}

impl HardwareManifest {
    pub fn load() -> Self {
        Self::load_from(Path::new(HARDWARE_MANIFEST_PATH))
    }

    pub fn load_from(path: &Path) -> Self {
        match fs::read_to_string(path) {
            Ok(text) => Self::from_json_str(&text).unwrap_or_default(),
            Err(_) => Self::default(),
        }
    }

    pub fn from_json_str(text: &str) -> Result<Self, String> {
        let j = Json::parse(text)?;
        let mut m = Self::default();

        if let Some(v) = j.get("schema").and_then(|x| x.as_i64()) {
            if v > 0 {
                m.schema = v as u32;
            }
        }
        if let Some(v) = j.get_str("board") {
            if !v.is_empty() {
                m.board = v.to_string();
            }
        }
        if let Some(v) = j.get_str("name") {
            if !v.is_empty() {
                m.name = v.to_string();
            }
        }
        if let Some(v) = j.get_str("vendor") {
            if !v.is_empty() {
                m.vendor = v.to_string();
            }
        }
        if let Some(v) = j.get_str("soc") {
            if !v.is_empty() {
                m.soc = v.to_string();
            }
        }
        if let Some(v) = j.get_str("dtb") {
            if !v.is_empty() {
                m.dtb = v.to_string();
            }
        }

        if let Some(disp) = j.get("display") {
            if let Some(v) = disp.get_str("type") {
                m.display_type = v.to_string();
            }
            if let Some(v) = disp.get("width").and_then(|x| x.as_i64()) {
                m.display_width = v.clamp(0, u16::MAX as i64) as u16;
            }
            if let Some(v) = disp.get("height").and_then(|x| x.as_i64()) {
                m.display_height = v.clamp(0, u16::MAX as i64) as u16;
            }
        }

        if let Some(ring) = j.get("led_ring") {
            if let Some(v) = ring.get("count").and_then(|x| x.as_i64()) {
                m.ring_leds = v.clamp(0, u16::MAX as i64) as u16;
            }
        }

        if let Some(audio) = j.get("audio") {
            if let Some(v) = audio.get("amp_i2c_bus").and_then(|x| x.as_i64()) {
                m.amp_i2c_bus = v.clamp(0, u8::MAX as i64) as u8;
            }
            if let Some(v) = audio.get_str("amp_tweeter_addr").and_then(parse_i2c_addr) {
                m.amp_tweeter_addr = v;
            }
            if let Some(v) = audio.get_str("amp_woofer_addr").and_then(parse_i2c_addr) {
                m.amp_woofer_addr = v;
            }
            if let Some(v) = audio.get("mic_channels").and_then(|x| x.as_i64()) {
                m.mic_channels = v.clamp(0, u8::MAX as i64) as u8;
            }
            if let Some(v) = audio.get_str("capture_device") {
                if !v.is_empty() {
                    m.capture_device = v.to_string();
                }
            }
        }

        if let Some(uarts) = j.get("uarts") {
            if let Some(v) = uarts.get_str("bluetooth") {
                m.bt_uart = v.to_string();
            }
            if let Some(v) = uarts.get_str("zigbee") {
                m.zigbee_uart = v.to_string();
            }
        }

        if let Some(feat) = j.get("features") {
            if let Some(v) = feat.get("rotary").and_then(|x| x.as_bool()) {
                m.has_rotary = v;
            }
            if let Some(v) = feat.get("als").and_then(|x| x.as_bool()) {
                m.has_als = v;
            }
            if let Some(v) = feat.get("zigbee").and_then(|x| x.as_bool()) {
                m.has_zigbee = v;
            }
            if let Some(v) = feat.get("bluetooth").and_then(|x| x.as_bool()) {
                m.has_bt = v;
            }
        }

        Ok(m)
    }

    pub fn has_display(&self) -> bool {
        self.display_type != "none" && self.display_width > 0 && self.display_height > 0
    }

    pub fn has_ring(&self) -> bool {
        self.ring_leds > 0
    }

    pub fn to_json(&self) -> Json {
        obj(vec![
            ("schema", Json::Num(self.schema as f64)),
            ("board", Json::Str(self.board.clone())),
            ("name", Json::Str(self.name.clone())),
            ("vendor", Json::Str(self.vendor.clone())),
            ("soc", Json::Str(self.soc.clone())),
            ("dtb", Json::Str(self.dtb.clone())),
            (
                "display",
                obj(vec![
                    ("type", Json::Str(self.display_type.clone())),
                    ("width", Json::Num(self.display_width as f64)),
                    ("height", Json::Num(self.display_height as f64)),
                ]),
            ),
            (
                "led_ring",
                obj(vec![("count", Json::Num(self.ring_leds as f64))]),
            ),
            (
                "audio",
                obj(vec![
                    ("amp_i2c_bus", Json::Num(self.amp_i2c_bus as f64)),
                    (
                        "amp_tweeter_addr",
                        Json::Str(format!("0x{:02x}", self.amp_tweeter_addr)),
                    ),
                    (
                        "amp_woofer_addr",
                        Json::Str(format!("0x{:02x}", self.amp_woofer_addr)),
                    ),
                    ("mic_channels", Json::Num(self.mic_channels as f64)),
                    ("capture_device", Json::Str(self.capture_device.clone())),
                ]),
            ),
            (
                "uarts",
                obj(vec![
                    ("bluetooth", Json::Str(self.bt_uart.clone())),
                    ("zigbee", Json::Str(self.zigbee_uart.clone())),
                ]),
            ),
            (
                "features",
                obj(vec![
                    ("rotary", Json::Bool(self.has_rotary)),
                    ("als", Json::Bool(self.has_als)),
                    ("zigbee", Json::Bool(self.has_zigbee)),
                    ("bluetooth", Json::Bool(self.has_bt)),
                ]),
            ),
        ])
    }
}

pub fn run(args: &[String]) -> i32 {
    let manifest = if let Some(path) = args.first() {
        HardwareManifest::load_from(Path::new(path))
    } else {
        HardwareManifest::load()
    };
    println!("{}", manifest.to_json().to_string_pretty());
    0
}
