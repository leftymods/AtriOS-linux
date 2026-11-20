use std::fs::{self, OpenOptions};
use std::io::Write;
use std::net::UdpSocket;
use std::path::Path;
use std::thread;
use std::time::{Duration, Instant, SystemTime};

use crate::status::active_error;
use crate::sys::{find_fb, find_lux_sensor, format_local};

pub const SCR_W: usize = 25;
pub const SCR_H: usize = 16;
pub const SCR_BPP: usize = 3;
pub const SCR_SIZE: usize = SCR_W * SCR_H * SCR_BPP;

pub const RING_LEDS: usize = 24;
pub const RING_SIZE: usize = RING_LEDS * 3;

const E_W: isize = 25;
const E_H: isize = 16;
const E_EYE_W: isize = 7;
const E_EYE_H: isize = 10;
const E_LEFT_X: isize = 3;
const E_RIGHT_X: isize = 15;
const E_BASE_Y: isize = 3;
const E_EQ_COLS: usize = 17;
const E_SLEEP_MS: f64 = 950.0;
const E_FPS: u64 = 15;

pub const BOOTING_FLAG_FILE: &str = "/var/run/atrios/booting";
pub const SCREEN_MODE_FILE: &str = "/var/run/atrios/screen_mode";
pub const VOICE_LEVEL_FILE: &str = "/var/run/atrios/voice_level";
pub const RING_OVERRIDE_FILE: &str = "/var/run/atrios/ring_override";
pub const BT_ACTIVE_FILE: &str = "/var/run/atrios/bt_active";
const EYES_UDP_ADDR: &str = "127.0.0.1:5055";

pub fn fb_screen() -> String {
    if let Ok(v) = std::env::var("ATRIOS_FB_SCREEN") {
        if !v.is_empty() {
            return v;
        }
    }
    find_fb(&["screen", "panel", "gowin", "yandex", "yndx led screen"], &["/dev/fb4", "/dev/fb0"])
}

pub fn fb_ring() -> String {
    if let Ok(v) = std::env::var("ATRIOS_FB_RING") {
        if !v.is_empty() {
            return v;
        }
    }
    find_fb(&["ring", "is31fl", "quasar-led-ring", "yndx led ring"], &["/dev/fb5", "/dev/fb1"])
}

pub fn screen_set_pixel(buf: &mut [u8], x: isize, y: isize, brightness: u8) {
    if x >= 0 && x < SCR_W as isize && y >= 0 && y < SCR_H as isize {
        let off = ((y as usize) * SCR_W + (x as usize)) * SCR_BPP;
        buf[off] = brightness;
        buf[off + 1] = brightness;
        buf[off + 2] = brightness;
    }
}

pub fn screen_send_frame_raw(buf: &[u8]) -> bool {
    let fb = fb_screen();
    let len = buf.len().min(SCR_SIZE);
    match OpenOptions::new().write(true).open(&fb) {
        Ok(mut f) => f.write_all(&buf[..len]).is_ok() && f.flush().is_ok(),
        Err(e) => {
            eprintln!("[Screen Error] Failed writing {fb}: {e}");
            false
        }
    }
}

pub fn screen_send_frame(buf: &[u8]) -> bool {
    if active_error().is_some() {
        return false;
    }
    screen_send_frame_raw(buf)
}

pub fn ring_send_frame(frame: &[u8]) -> bool {
    if active_error().is_some() || Path::new(RING_OVERRIDE_FILE).exists() {
        return false;
    }
    let fb = fb_ring();
    let len = frame.len().min(RING_SIZE);
    match OpenOptions::new().write(true).open(&fb) {
        Ok(mut f) => f.write_all(&frame[..len]).is_ok() && f.flush().is_ok(),
        Err(e) => {
            eprintln!("[Ring Error] Failed writing {fb}: {e}");
            false
        }
    }
}

pub fn is_bt_active() -> bool {
    fs::read_to_string(BT_ACTIVE_FILE)
        .map(|s| s.trim() == "1")
        .unwrap_or(false)
}

fn clock_glyph(ch: char) -> [u8; 9] {
    match ch {
        '0' => [0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110],
        '1' => [0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110],
        '2' => [0b01110, 0b10001, 0b00001, 0b00001, 0b00110, 0b01000, 0b10000, 0b10000, 0b11111],
        '3' => [0b01110, 0b10001, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b10001, 0b01110],
        '4' => [0b10001, 0b10001, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b00001, 0b00001],
        '5' => [0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b00001, 0b10001, 0b01110],
        '6' => [0b01110, 0b10001, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b01110],
        '7' => [0b11111, 0b00001, 0b00001, 0b00010, 0b00010, 0b00100, 0b00100, 0b00100, 0b00100],
        '8' => [0b01110, 0b10001, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110],
        '9' => [0b01110, 0b10001, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b10001, 0b01110],
        ':' => [0b0, 0b0, 0b1, 0b0, 0b0, 0b0, 0b1, 0b0, 0b0],
        _ => [0; 9],
    }
}

pub fn render_clock_frame(now_hm: &str, bt: bool, brightness: u8) -> Vec<u8> {
    let mut buf = vec![0u8; SCR_SIZE];
    let x_pos = [0isize, 6, 12, 14, 20];
    for (ch, &xs) in now_hm.chars().zip(x_pos.iter()) {
        let glyph = clock_glyph(ch);
        let width: isize = if ch == ':' { 1 } else { 5 };
        for (row_idx, &bits) in glyph.iter().enumerate() {
            let y = 3 + row_idx as isize;
            for col in 0..width {
                if ((bits >> ((width - 1 - col) as u8)) & 1) != 0 {
                    screen_set_pixel(&mut buf, xs + col, y, brightness);
                }
            }
        }
    }
    if bt {
        let bt_glyph = [0b010u8, 0b110, 0b010, 0b110, 0b010];
        for (r, &bits) in bt_glyph.iter().enumerate() {
            for c in 0..3isize {
                if ((bits >> (2 - c)) & 1) != 0 {
                    screen_set_pixel(&mut buf, 21 + c, r as isize, brightness);
                }
            }
        }
    }
    buf
}

fn font_5x7(ch: char) -> [u8; 7] {
    match ch {
        '0' => [0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110],
        '1' => [0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110],
        '2' => [0b01110, 0b10001, 0b00001, 0b00110, 0b01000, 0b10000, 0b11111],
        '3' => [0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110],
        '4' => [0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010],
        '5' => [0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110],
        '6' => [0b01110, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b01110],
        '7' => [0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000],
        '8' => [0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110],
        '9' => [0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100],
        ':' => [0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000],
        '-' => [0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000],
        '.' => [0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00100],
        '!' => [0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100],
        'A' => [0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001],
        'B' => [0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110],
        'C' => [0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110],
        'D' => [0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100],
        'E' => [0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111],
        'F' => [0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000],
        'G' => [0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110],
        'H' => [0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001],
        'I' => [0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110],
        'J' => [0b00001, 0b00001, 0b00001, 0b00001, 0b10001, 0b10001, 0b01110],
        'K' => [0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001],
        'L' => [0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111],
        'M' => [0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001],
        'N' => [0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001],
        'O' => [0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110],
        'P' => [0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000],
        'Q' => [0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101],
        'R' => [0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001],
        'S' => [0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110],
        'T' => [0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100],
        'U' => [0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110],
        'V' => [0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100],
        'W' => [0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001],
        'X' => [0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001],
        'Y' => [0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100],
        'Z' => [0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111],
        _ => [0; 7],
    }
}

pub fn screen_clock(brightness: u8) {
    let hm = format_local("%H:%M", None);
    let buf = render_clock_frame(&hm, is_bt_active(), brightness);
    screen_send_frame(&buf);
    println!("[Screen] Clock: {hm} (brightness={brightness}/255)");
}

const BOYKISSER_F1: [&str; 16] = [
    "  #                   #  ",
    " ###                 ### ",
    " # ##       #       ## # ",
    " #  ##     ###     ##  # ",
    " #   ##   #####   ##   # ",
    "#     #############     #",
    "##    ###       ###    ##",
    " #   ####       ####   # ",
    " #   ####       ####   # ",
    "##   ###         ###   ##",
    "#  ##     #   #     ##  #",
    " ##        # #        ## ",
    "   ##       #       ##   ",
    "    ###           ###    ",
    "     #             #     ",
    "                         ",
];

const BOYKISSER_F2: [&str; 16] = [
    "  #                   #  ",
    " ###                 ### ",
    " # ##       #       ## # ",
    " #  ##     ###     ##  # ",
    " #   ##   #####   ##   # ",
    "#     #############     #",
    "##                     ##",
    " #   ##           ##   # ",
    " #    ####     ####    # ",
    "##   ##   #####   ##   ##",
    "#  ##    #     #    ##  #",
    " ##       #   #       ## ",
    "   ##      ###      ##   ",
    "    ###           ###    ",
    "     #             #     ",
    "                         ",
];

const BOYKISSER_BLINK: [&str; 16] = [
    "  #                   #  ",
    " ###                 ### ",
    " # ##       #       ## # ",
    " #  ##     ###     ##  # ",
    " #   ##   #####   ##   # ",
    "#     #############     #",
    "##                     ##",
    " #   ####       ####   # ",
    " #                     # ",
    "##                     ##",
    "#  ##     #   #     ##  #",
    " ##        # #        ## ",
    "   ##       #       ##   ",
    "    ###           ###    ",
    "     #             #     ",
    "                         ",
];

fn render_matrix_pattern(
    pattern: &[&str; 16],
    brightness: u8,
    blush: &[(usize, usize)],
    hi: &[(usize, usize)],
) -> Vec<u8> {
    let mut buf = vec![0u8; SCR_SIZE];
    for (y, row) in pattern.iter().enumerate() {
        for (x, ch) in row.chars().enumerate() {
            if x >= SCR_W {
                break;
            }
            if ch == '#' {
                let mut val = brightness;
                if hi.contains(&(x, y)) {
                    val = 255;
                } else if blush.contains(&(x, y)) {
                    val = ((brightness as f64 * 0.45) as u8).max(50);
                }
                screen_set_pixel(&mut buf, x as isize, y as isize, val);
            }
        }
    }
    buf
}

fn screen_boykisser(mode: &str, brightness: u8) {
    let blush = [(3, 10), (4, 10), (20, 10), (21, 10)];
    let hi = [(7, 6), (17, 6)];
    let f1 = render_matrix_pattern(&BOYKISSER_F1, brightness, &blush, &hi);
    let f2 = render_matrix_pattern(&BOYKISSER_F2, brightness, &blush, &[]);
    let blink = render_matrix_pattern(&BOYKISSER_BLINK, brightness, &blush, &[]);
    match mode {
        "1" | "calm" | "frame1" => {
            screen_send_frame(&f1);
            println!("[Screen] Boykisser Frame 1 (Calm :3)");
        }
        "2" | "happy" | "frame2" => {
            screen_send_frame(&f2);
            println!("[Screen] Boykisser Frame 2 (Happy >< :D)");
        }
        _ => {
            println!("[Screen] Playing Boykisser animation...");
            loop {
                screen_send_frame(&f1);
                thread::sleep(Duration::from_millis(1200));
                screen_send_frame(&blink);
                thread::sleep(Duration::from_millis(120));
                screen_send_frame(&f1);
                thread::sleep(Duration::from_millis(600));
                screen_send_frame(&f2);
                thread::sleep(Duration::from_millis(1400));
                screen_send_frame(&f1);
                thread::sleep(Duration::from_millis(250));
                screen_send_frame(&f2);
                thread::sleep(Duration::from_millis(900));
            }
        }
    }
}

fn dig3(ch: char) -> &'static [&'static str] {
    match ch {
        '0' => &["###", "#.#", "#.#", "#.#", "###"],
        '1' => &[".#.", "##.", ".#.", ".#.", "###"],
        '2' => &["###", "..#", "###", "#..", "###"],
        '3' => &["###", "..#", "###", "..#", "###"],
        '4' => &["#.#", "#.#", "###", "..#", "..#"],
        '5' => &["###", "#..", "###", "..#", "###"],
        '6' => &["###", "#..", "###", "#.#", "###"],
        '7' => &["###", "..#", "..#", ".#.", ".#."],
        '8' => &["###", "#.#", "###", "#.#", "###"],
        '9' => &["###", "#.#", "###", "..#", "###"],
        _ => &[],
    }
}

const E_ARCH: &[&str] = &["..###..", ".#####.", "#######", "##...##", "#.....#"];
const E_WINK_ARCH: &[&str] = &["..###..", ".#####.", "##...##"];
const E_CIRCLE8: &[&str] = &[
    "..####..", ".######.", "########", "########", "########", "########", ".######.", "..####..",
];
const E_CIRCLE9: &[&str] = &[
    "..#####..", ".#######.", "#########", "#########", "#########", "#########", "#########",
    ".#######.", "..#####..",
];
const E_HEART_BIG: &[&str] = &[".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."];
const E_HEART_SMALL: &[&str] = &[".#.#.", "#####", ".###.", "..#.."];
const E_BT_SMALL: &[&str] = &[
    "..#..", "..##.", "#.#.#", ".###.", "..#..", ".###.", "#.#.#", "..##.", "..#..",
];
const E_BT_BIG: &[&str] = &[
    "...#...", "...##..", "#..#.#.", ".#.#.#.", "..###..", "...#...", "..###..", ".#.#.#.",
    "#..#.#.", "...##..", "...#...",
];
const E_MIC: &[&str] = &[
    "..###..", ".#####.", ".#####.", ".#####.", "..###..", "#.....#", ".#####.", "...#...",
    "..###..",
];
const E_SPEAKER: &[&str] = &["...#", "..##", "####", "####", "####", "..##", "...#"];
const E_BELL: &[&str] = &[
    "...#...", "..###..", ".#####.", ".#####.", ".#####.", "#######", "...#...",
];
const E_SUN: &[&str] = &[".###.", "#####", "#####", "#####", ".###."];
const E_CLOUD: &[&str] = &["..###..", ".#####.", "#######", "#######"];
const E_PHONE: &[&str] = &["#####", "#...#", "#.#.#", "#...#", "#...#", "#.#.#", "#####"];

const E_X_SPRITE: &[&str] = &[
    "##...##", "###.###", ".#####.", "..###..", ".#####.", "###.###", "##...##",
];
const E_X_SMALL: &[&str] = &["##...##", ".##.##.", "..###..", ".##.##.", "##...##"];
const E_WIFI: &[&str] = &[
    "..#######..", ".#.......#.", "#..#####..#", "..#.....#..", "....###....", "...........",
    ".....#.....", "....###....",
];
const E_GLOBE: &[&str] = &[
    "..#####..", ".#.#.#.#.", "#########", "#..#.#..#", "#########", ".#.#.#.#.", "..#####..",
];
const E_THERM: &[&str] = &[".###.", ".#.#.", ".#.#.", ".###.", ".###.", "#####", "#####", ".###."];

#[inline]
fn rnd(v: f64) -> isize {
    (v + 0.5).floor() as isize
}

struct Rng(u64);
impl Rng {
    fn next_f64(&mut self) -> f64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x.max(1);
        ((x >> 11) as f64) / ((1u64 << 53) as f64)
    }
}

pub struct Eyes {
    pub pixels: [[u8; SCR_W]; SCR_H],
    pub state: String,
    pub variant: String,
    pub voice_level: Option<f64>,
    pub eq: Option<Vec<f64>>,
    pub volume: i32,
    pub boot_start: f64,
    pub boot_orbit_end: Option<f64>,
    pub setup_start: f64,
    pub sleep_mode: &'static str,
    pub sleep_start: f64,
    pub manual_gaze: bool,
    pub tdx: isize,
    pub tdy: isize,
    cdx: f64,
    cdy: f64,
    next_look: f64,
    next_blink: f64,
    blink_start: f64,
    double_blink: bool,
    peaks: [f64; E_EQ_COLS],
    smooth: [f64; E_EQ_COLS],
    disp_vol: f64,
    rng: Rng,
}

impl Eyes {
    pub fn new(now_ms: f64) -> Self {
        Self {
            pixels: [[0u8; SCR_W]; SCR_H],
            state: "idle".into(),
            variant: "default".into(),
            voice_level: None,
            eq: None,
            volume: 50,
            boot_start: now_ms,
            boot_orbit_end: None,
            setup_start: now_ms,
            sleep_mode: "none",
            sleep_start: 0.0,
            manual_gaze: false,
            tdx: 0,
            tdy: 0,
            cdx: 0.0,
            cdy: 0.0,
            next_look: now_ms + 2000.0,
            next_blink: now_ms + 2800.0,
            blink_start: -1.0,
            double_blink: false,
            peaks: [0.0; E_EQ_COLS],
            smooth: [1.0; E_EQ_COLS],
            disp_vol: 50.0,
            rng: Rng(0x9e37_79b9_7f4a_7c15),
        }
    }

    pub fn set_state(&mut self, state: &str, variant: Option<&str>, now_ms: f64) {
        if let Some(v) = variant {
            if matches!(v, "default" | "happy" | "angry" | "sad" | "surprised") {
                self.variant = v.to_string();
            }
        }
        let prev = std::mem::replace(&mut self.state, state.to_string());
        if state == "boot" {
            self.boot_start = now_ms;
            self.boot_orbit_end = None;
        } else if state == "setup" {
            self.setup_start = now_ms;
        }
        let was_eye = matches!(
            prev.as_str(),
            "idle"
                | "listening"
                | "thinking"
                | "speaking"
                | "happy"
                | "love"
                | "surprised"
                | "wink"
                | "cool"
                | "confused"
                | "angry"
                | "sad"
        );
        if state == "sleep" && was_eye {
            self.sleep_mode = "falling_asleep";
            self.sleep_start = now_ms;
        } else if state == "idle" && prev == "sleep" {
            self.sleep_mode = "waking";
            self.sleep_start = now_ms;
        } else {
            self.sleep_mode = "none";
        }
    }

    pub fn boot_done(&self, now_ms: f64) -> bool {
        match self.boot_orbit_end {
            Some(oe) => ((now_ms - self.boot_start) / 1000.0 - oe) >= 2.70,
            None => false,
        }
    }

    fn px(&mut self, x: isize, y: isize, v: u8) {
        if x >= 0 && x < E_W && y >= 0 && y < E_H {
            self.pixels[y as usize][x as usize] = if v != 0 { 1 } else { 0 };
        }
    }

    fn rect(&mut self, x0: isize, y0: isize, w: isize, h: isize, r: isize, v: u8) {
        for dy in 0..h {
            for dx in 0..w {
                if r > 0 && w >= 3 && h >= 3 && (dx < r || dx >= w - r) && (dy < r || dy >= h - r) {
                    continue;
                }
                self.px(x0 + dx, y0 + dy, v);
            }
        }
    }

    fn line(&mut self, mut x0: isize, mut y0: isize, x1: isize, y1: isize, v: u8) {
        let dx = (x1 - x0).abs();
        let dy = -(y1 - y0).abs();
        let sx = if x0 < x1 { 1 } else { -1 };
        let sy = if y0 < y1 { 1 } else { -1 };
        let mut err = dx + dy;
        loop {
            self.px(x0, y0, v);
            if x0 == x1 && y0 == y1 {
                break;
            }
            let e2 = 2 * err;
            if e2 >= dy {
                err += dy;
                x0 += sx;
            }
            if e2 <= dx {
                err += dx;
                y0 += sy;
            }
        }
    }

    fn bmp(&mut self, x0: isize, y0: isize, rows: &[&str]) {
        for (r, row) in rows.iter().enumerate() {
            for (c, ch) in row.chars().enumerate() {
                if ch == '#' {
                    self.px(x0 + c as isize, y0 + r as isize, 1);
                }
            }
        }
    }

    fn lash(&mut self, x: isize, y: isize, w: isize, right: bool) {
        if !right {
            self.px(x - 1, y + 1, 1);
            self.px(x - 2, y, 1);
        } else {
            self.px(x + w, y + 1, 1);
            self.px(x + w + 1, y, 1);
        }
    }

    fn eye(&mut self, x: isize, y: isize, w: isize, h: isize, slant: isize, right: bool, cheek: isize) {
        if h <= 2 {
            self.rect(x, y, w, h.max(1), 0, 1);
            return;
        }
        self.rect(x, y, w, h, 1, 1);
        if h >= 5 {
            let ly = if slant < 0 { y + slant.abs() } else { y };
            self.lash(x, ly, w, right);
        }
        if slant != 0 {
            let denom = (w - 1).max(1) as f64;
            for dx in 0..w {
                let n = (dx as f64) / denom;
                let f = if slant > 0 {
                    if right { 1.0 - n } else { n }
                } else if right {
                    n
                } else {
                    1.0 - n
                };
                for dy in 0..rnd(f * (slant.abs() as f64)) {
                    self.px(x + dx, y + dy, 0);
                }
            }
        }
        if cheek > 0 && h >= 5 {
            let mid = (w - 1) as f64 / 2.0;
            for dx in 1..(w - 1) {
                if ((dx as f64) - mid).abs() <= 1.6 {
                    for k in 0..cheek {
                        self.px(x + dx, y + h - 1 - k, 0);
                    }
                }
            }
        }
    }

    fn level(&self, t: f64, speed: f64) -> f64 {
        self.voice_level
            .unwrap_or_else(|| 0.55 + 0.45 * (t * (speed * 0.43)).sin())
    }

    fn strip(&mut self, t: f64, speed: f64, amp: f64, freq: f64) {
        let lvl = self.level(t, speed);
        let eff_amp = if self.voice_level.is_some() { amp * 1.35 } else { amp };
        let y_min = if self.voice_level.is_some() { 11 } else { 12 };
        for x in 7isize..18 {
            if lvl < 0.06 {
                self.px(x, 14, 1);
                continue;
            }
            let xf = x as f64;
            let dist = (xf - 12.0).abs() / 5.0;
            let w = (xf * freq - t * speed).sin() + 0.5 * (xf * freq * 1.6 + t * speed * 1.25).cos();
            let off = rnd(w * eff_amp * (1.0 - 0.55 * dist * dist) * lvl);
            let py = (14 + off).clamp(y_min, 15);
            self.px(x, py, 1);
            if off.abs() >= 1 && dist < 0.6 && lvl > 0.65 {
                self.px(x, 14, 1);
            }
        }
    }

    fn blink(&mut self, now: f64) -> f64 {
        if now >= self.next_blink && self.blink_start < 0.0 {
            self.blink_start = now;
            self.double_blink = self.rng.next_f64() < 0.25;
            self.next_blink = now + 2800.0 + self.rng.next_f64() * 3200.0;
        }
        if self.blink_start >= 0.0 {
            let el = now - self.blink_start;
            if (0.0..210.0).contains(&el) {
                return (el / 210.0 * 2.0 - 1.0).abs();
            }
            if el >= 210.0 {
                self.blink_start = -1.0;
                if self.double_blink {
                    self.double_blink = false;
                    self.blink_start = now + 80.0;
                }
            }
        }
        1.0
    }

    fn sleep_tr(&mut self, now: f64, waking: bool) {
        let el = now - self.sleep_start;
        let p = (el / E_SLEEP_MS).clamp(0.0, 1.0);
        let ease = p * p * (3.0 - 2.0 * p);
        let op = if waking { ease } else { 1.0 - ease };
        if op <= 0.18 {
            for bx in [E_LEFT_X, E_RIGHT_X] {
                self.px(bx, 9, 1);
                self.line(bx + 1, 10, bx + 5, 10, 1);
                self.px(bx + 6, 9, 1);
            }
        } else {
            let h = rnd(2.0 + ((E_EYE_H - 2) as f64) * op).max(2);
            let y = rnd((E_BASE_Y as f64) + (1.0 - op) * 5.0);
            self.eye(E_LEFT_X, y, E_EYE_W, h, 0, false, 0);
            self.eye(E_RIGHT_X, y, E_EYE_W, h, 0, true, 0);
        }
        if el >= E_SLEEP_MS {
            self.sleep_mode = "none";
            if waking {
                self.blink_start = now;
                self.double_blink = true;
            }
        }
    }

    fn beam(&mut self, p: f64) {
        let hw = rnd(((p * 1.3).min(1.0) * std::f64::consts::FRAC_PI_2).sin() * 12.0);
        self.line(12 - hw, 7, 12 + hw, 7, 1);
        if p > 0.3 && p < 0.7 {
            let b = rnd((hw as f64) * 0.5);
            self.line(12 - b, 6, 12 + b, 6, 1);
            self.line(12 - b, 8, 12 + b, 8, 1);
        }
    }

    fn orbits(&mut self, sub: f64) {
        for orb in 0..2 {
            let mut prev: Option<(isize, isize)> = None;
            for step in 0..6 {
                let a = sub * 3.8 + (orb as f64) * std::f64::consts::PI - (step as f64) * 0.14;
                let ox = rnd(12.0 + a.sin() * 8.5);
                let oy = rnd(7.5 + (a * 2.0).sin() * 4.2);
                if let Some((px, py)) = prev {
                    self.line(px, py, ox, oy, 1);
                }
                if step == 0 {
                    self.rect(ox, oy, 2, 2, 0, 1);
                }
                prev = Some((ox, oy));
            }
        }
    }

    pub fn render(&mut self, now: f64) {
        let t = now / 1000.0;
        self.pixels = [[0u8; SCR_W]; SCR_H];
        self.cdx += ((self.tdx as f64) - self.cdx) * 0.28;
        self.cdy += ((self.tdy as f64) - self.cdy) * 0.28;
        let dx = rnd(self.cdx);
        let dy = rnd(self.cdy);
        let bo = self.blink(now);

        match self.state.as_str() {
            "idle" => {
                if self.sleep_mode == "waking" {
                    self.sleep_tr(now, true);
                    return;
                }
                if !self.manual_gaze && now >= self.next_look {
                    let choices = [-1isize, 0, 0, 1];
                    self.tdx = choices[(self.rng.next_f64() * 4.0) as usize % 4];
                    self.tdy = choices[(self.rng.next_f64() * 4.0) as usize % 4];
                    self.next_look = now + 1400.0 + self.rng.next_f64() * 2200.0;
                }
                let h = rnd(1.0 + ((E_EYE_H - 1) as f64) * bo).max(1);
                let y = E_BASE_Y + dy + (E_EYE_H - h) / 2;
                self.eye(E_LEFT_X + dx, y, E_EYE_W, h, 0, false, 0);
                self.eye(E_RIGHT_X + dx, y, E_EYE_W, h, 0, true, 0);
            }
            "listening" => {
                let extra = if (t * 2.8).sin() > 0.25 { 1.0 } else { 0.0 };
                let h = rnd(1.0 + (10.0 + extra) * bo).max(1);
                let y = (E_H - h) / 2;
                let tilt = rnd((t * 1.4).sin() * 0.6);
                self.eye(2 + tilt, y, 8, h, 0, false, 0);
                self.eye(15 + tilt, y, 8, h, 0, true, 0);
            }
            "thinking" => {
                let c = (t * 1.25).sin();
                let sh = if c > 0.35 { 1 } else if c < -0.35 { -1 } else { 0 };
                let (lb, rb) = if sh >= 0 { (5isize, 8isize) } else { (8, 5) };
                let lh = rnd(1.0 + ((lb - 1) as f64) * bo).max(1);
                let rh = rnd(1.0 + ((rb - 1) as f64) * bo).max(1);
                self.eye(E_LEFT_X + sh, (if sh >= 0 { 4 } else { 1 }) + (lb - lh) / 2, E_EYE_W, lh, 0, false, 0);
                self.eye(E_RIGHT_X + sh, (if sh >= 0 { 1 } else { 4 }) + (rb - rh) / 2, E_EYE_W, rh, 0, true, 0);
                for i in 0..3isize {
                    let y = if (t * 6.0 - (i as f64) * 1.1).sin() > 0.45 { 13 } else { 14 };
                    self.px(9 + i * 3, y, 1);
                }
            }
            "speaking" => match self.variant.as_str() {
                "happy" => {
                    let bob = if self.level(t, 13.5) > 0.55 || (t * 6.5).sin() > 0.25 { 0 } else { 1 };
                    self.bmp(E_LEFT_X, 3 + bob, E_ARCH);
                    self.bmp(E_RIGHT_X, 3 + bob, E_ARCH);
                    self.lash(E_LEFT_X, 4 + bob, E_EYE_W, false);
                    self.lash(E_RIGHT_X, 4 + bob, E_EYE_W, true);
                    self.strip(t, 13.5, 1.15, 0.85);
                }
                "angry" => {
                    let press = if self.level(t, 19.0) > 0.6 { 1 } else { 0 };
                    let bh = 8 - press;
                    let h = rnd(1.0 + ((bh - 1) as f64) * bo).max(1);
                    let y = 2 + press + (bh - h) / 2;
                    self.eye(E_LEFT_X, y, E_EYE_W, h, 4, false, 0);
                    self.eye(E_RIGHT_X, y, E_EYE_W, h, 4, true, 0);
                    self.strip(t, 19.0, 1.35, 1.05);
                }
                "sad" => {
                    let h = rnd(1.0 + 6.0 * bo).max(1);
                    let y = 2 + (if (t * 2.0).sin() > 0.4 { 1 } else { 0 }) + (7 - h) / 2;
                    self.eye(E_LEFT_X, y, E_EYE_W, h, -3, false, 0);
                    self.eye(E_RIGHT_X, y, E_EYE_W, h, -3, true, 0);
                    let tear = ((t * 3.5) as isize).rem_euclid(6);
                    if tear < 4 {
                        self.px(E_LEFT_X + 1, 9 + tear, 1);
                    }
                    self.strip(t, 6.0, 0.8, 0.6);
                }
                "surprised" => {
                    let lift = if self.level(t, 14.5) > 0.6 { 0 } else { 1 };
                    if bo < 0.35 {
                        self.line(2, 5 + lift, 9, 5 + lift, 1);
                        self.line(15, 5 + lift, 22, 5 + lift, 1);
                    } else {
                        self.bmp(2, 1 + lift, E_CIRCLE8);
                        self.bmp(15, 1 + lift, E_CIRCLE8);
                        self.lash(2, 2 + lift, 8, false);
                        self.lash(15, 2 + lift, 8, true);
                    }
                    self.strip(t, 14.5, 1.2, 0.9);
                }
                _ => {
                    let lvl = self.level(t, 12.0);
                    let lift = if lvl > 0.62 { 0 } else { 1 };
                    let glance = rnd((t * 1.8).sin() * 0.6);
                    let bh = if lvl > 0.55 { 8 } else { 7 };
                    let h = rnd(1.0 + ((bh - 1) as f64) * bo).max(1);
                    let y = 2 + lift + (bh - h) / 2;
                    self.eye(E_LEFT_X + glance, y, E_EYE_W, h, 0, false, 2);
                    self.eye(E_RIGHT_X + glance, y, E_EYE_W, h, 0, true, 2);
                    self.strip(t, 12.0, 1.1, 0.78);
                }
            },
            "happy" => {
                let b = if (t * 5.2).sin() > 0.1 { 0 } else { 1 };
                self.bmp(E_LEFT_X, 5 + b, E_ARCH);
                self.bmp(E_RIGHT_X, 5 + b, E_ARCH);
                self.lash(E_LEFT_X, 6 + b, E_EYE_W, false);
                self.lash(E_RIGHT_X, 6 + b, E_EYE_W, true);
            }
            "love" => {
                let ph = (t * 1.15).rem_euclid(1.0);
                if ph < 0.16 || (0.26..0.42).contains(&ph) {
                    self.bmp(E_LEFT_X, 5, E_HEART_BIG);
                    self.bmp(E_RIGHT_X, 5, E_HEART_BIG);
                } else {
                    self.bmp(E_LEFT_X + 1, 6, E_HEART_SMALL);
                    self.bmp(E_RIGHT_X + 1, 6, E_HEART_SMALL);
                }
            }
            "surprised" => {
                let lift = if (t * 2.5).sin() > 0.5 { 0 } else { 1 };
                if bo < 0.35 {
                    self.line(2, 7 + lift, 10, 7 + lift, 1);
                    self.line(14, 7 + lift, 22, 7 + lift, 1);
                } else {
                    self.bmp(2, 3 + lift, E_CIRCLE9);
                    self.bmp(14, 3 + lift, E_CIRCLE9);
                    self.lash(2, 4 + lift, 9, false);
                    self.lash(14, 4 + lift, 9, true);
                }
            }
            "wink" => {
                let bob = if (t * 3.0).sin() > 0.3 { 0 } else { 1 };
                self.eye(E_LEFT_X, E_BASE_Y + bob, E_EYE_W, E_EYE_H - 1, 0, false, 1);
                if (t * 0.7).rem_euclid(1.0) < 0.82 {
                    self.bmp(E_RIGHT_X, 7 + bob, E_WINK_ARCH);
                    self.lash(E_RIGHT_X, 7 + bob, E_EYE_W, true);
                } else {
                    self.eye(E_RIGHT_X, E_BASE_Y + bob, E_EYE_W, E_EYE_H - 1, 0, true, 0);
                }
            }
            "cool" => {
                let y = 6 + if (t * 2.2).sin() > 0.6 { -1 } else { 0 };
                for bx in [E_LEFT_X, E_RIGHT_X] {
                    self.rect(bx, y, E_EYE_W, 5, 1, 1);
                    self.line(bx, y, bx + E_EYE_W - 1, y, 1);
                }
                self.lash(E_LEFT_X, y, E_EYE_W, false);
                self.lash(E_RIGHT_X, y, E_EYE_W, true);
                let shine = ((t * 7.0) as isize).rem_euclid(13);
                for k in 0..3isize {
                    if (0..E_EYE_W).contains(&(shine - k)) {
                        self.px(E_LEFT_X + shine - k, y + k, 0);
                        self.px(E_RIGHT_X + shine - k, y + k, 0);
                    }
                }
            }
            "confused" => {
                let tilt = if (t * 2.4).sin() > 0.0 { 1 } else { 0 };
                let lh = rnd(1.0 + 5.0 * bo).max(1);
                let rh = rnd(1.0 + 9.0 * bo).max(1);
                self.eye(E_LEFT_X, 5 + tilt + (6 - lh) / 2, E_EYE_W, lh, 2, false, 0);
                self.eye(E_RIGHT_X, 2 + (10 - rh) / 2, E_EYE_W, rh, 0, true, 0);
            }
            "angry" => {
                let press = if (t * 5.5).sin() > 0.35 { 1 } else { 0 };
                let bh = 9 - press;
                let h = rnd(1.0 + ((bh - 1) as f64) * bo).max(1);
                let y = 4 + press + (bh - h) / 2;
                self.eye(E_LEFT_X, y, E_EYE_W, h, 4, false, 0);
                self.eye(E_RIGHT_X, y, E_EYE_W, h, 4, true, 0);
            }
            "sad" => {
                let h = rnd(1.0 + 6.0 * bo).max(1);
                let y = 4 + (if (t * 1.8).sin() > 0.4 { 1 } else { 0 }) + (7 - h) / 2;
                self.eye(E_LEFT_X, y, E_EYE_W, h, -3, false, 0);
                self.eye(E_RIGHT_X, y, E_EYE_W, h, -3, true, 0);
                for (off, bx) in [(0.0, E_LEFT_X + 1), (3.5, E_RIGHT_X + E_EYE_W - 2)] {
                    let ph = ((t * 4.0 + off) as isize).rem_euclid(7);
                    if ph < 5 {
                        self.px(bx, 11 + ph, 1);
                        if 12 + ph < E_H {
                            self.px(bx, 12 + ph, 1);
                        }
                    }
                }
            }
            "sleep" => {
                if self.sleep_mode == "falling_asleep" {
                    self.sleep_tr(now, false);
                    return;
                }
                let y = 9 + if (t * 1.5).sin() > 0.0 { 0 } else { 1 };
                for bx in [E_LEFT_X, E_RIGHT_X] {
                    self.px(bx, y, 1);
                    self.line(bx + 1, y + 1, bx + 5, y + 1, 1);
                    self.px(bx + 6, y, 1);
                }
                let zy = 4 - ((t * 1.8) as isize).rem_euclid(4);
                if zy >= 1 {
                    self.line(21, zy, 23, zy, 1);
                    self.px(22, zy + 1, 1);
                    self.line(21, zy + 2, 23, zy + 2, 1);
                }
            }
            "boot" => {
                let ph = (now - self.boot_start) / 1000.0;
                if ph < 0.75 {
                    self.beam(ph / 0.75);
                    return;
                }
                if self.boot_orbit_end.is_none() {
                    if ph >= 2.85 && (!Path::new(BOOTING_FLAG_FILE).exists() || ph >= 25.0) {
                        self.boot_orbit_end = Some(ph);
                    } else {
                        self.orbits(ph - 0.75);
                        return;
                    }
                }
                let oe = self.boot_orbit_end.unwrap();
                let post = ph - oe;
                if post < 0.70 {
                    let p = post / 0.70;
                    let ease = p * p * (3.0 - 2.0 * p);
                    let ea = (oe - 0.75) * 3.8;
                    let ax = 12.0 + ea.sin() * 8.5;
                    let ay = 7.5 + (ea * 2.0).sin() * 4.2;
                    let bx = 12.0 + (ea + std::f64::consts::PI).sin() * 8.5;
                    let by = 7.5 + ((ea + std::f64::consts::PI) * 2.0).sin() * 4.2;
                    let (l, r) = if ax < bx { ((ax, ay), (bx, by)) } else { ((bx, by), (ax, ay)) };
                    let lx = rnd(l.0 + (6.0 - l.0) * ease);
                    let ly = rnd(l.1 + (8.0 - l.1) * ease);
                    let rx = rnd(r.0 + (18.0 - r.0) * ease);
                    let ry = rnd(r.1 + (8.0 - r.1) * ease);
                    let hl = rnd(1.0 + ease * 2.0);
                    self.line(lx - hl, ly, lx + hl, ly, 1);
                    self.line(rx - hl, ry, rx + hl, ry, 1);
                } else {
                    let sub = post - 0.70;
                    if sub < 1.45 {
                        let mut h = rnd(1.0 + ((E_EYE_H - 1) as f64) * (sub / 0.45).min(1.0)).max(1);
                        if (0.60..0.72).contains(&sub) || (0.84..0.96).contains(&sub) {
                            h = 1;
                        }
                        let y = E_BASE_Y + (E_EYE_H - h) / 2;
                        self.eye(E_LEFT_X, y, E_EYE_W, h, 0, false, 0);
                        self.eye(E_RIGHT_X, y, E_EYE_W, h, 0, true, 0);
                    } else if sub < 1.72 {
                        // Vertical eye collapse (10 -> 1)
                        let p = ((sub - 1.45) / 0.27).clamp(0.0, 1.0);
                        let ease = p * p;
                        let h = rnd((E_EYE_H as f64) - ((E_EYE_H - 1) as f64) * ease).max(1);
                        let y = E_BASE_Y + (E_EYE_H - h) / 2;
                        self.eye(E_LEFT_X, y, E_EYE_W, h, 0, false, 0);
                        self.eye(E_RIGHT_X, y, E_EYE_W, h, 0, true, 0);
                    } else if sub < 1.90 {
                        // Horizontal slit collapse toward eye centers (7 -> 0 -> completely off)
                        let p = ((sub - 1.72) / 0.18).clamp(0.0, 1.0);
                        if p < 0.90 {
                            let hw = rnd(3.0 * (1.0 - p / 0.90)).max(0);
                            self.line(6 - hw, 7, 6 + hw, 7, 1);
                            self.line(18 - hw, 7, 18 + hw, 7, 1);
                        }
                    }
                }
            }
            "setup" => {
                let el = (now - self.setup_start) / 1000.0;
                if el < 0.75 {
                    self.beam(el / 0.75);
                } else if el < 2.65 {
                    self.orbits(el - 0.75);
                } else if el < 3.35 {
                    let p = (el - 2.65) / 0.7;
                    let ease = p * p * (3.0 - 2.0 * p);
                    let lx = rnd(8.0 - ease * 6.0);
                    let rx = rnd(14.0 + ease * 3.0);
                    self.line(lx, 4, lx + 4, 4, 1);
                    self.line(lx, 10, lx + 4, 10, 1);
                    self.line(lx, 4, lx, 10, 1);
                    self.line(lx + 4, 4, lx + 4, 10, 1);
                    self.px(rx + 2, 10, 1);
                    if p > 0.35 {
                        self.bmp(rx, 7, &[".###.", "#...#"]);
                    }
                    if p > 0.7 {
                        self.bmp(rx - 1, 4, &[".#####.", "#.....#"]);
                    }
                } else {
                    self.bmp(2, 4, E_PHONE);
                    let pk = 8 + ((t * 9.0) as isize).rem_euclid(8);
                    self.px(pk, 7, 1);
                    if pk > 8 {
                        self.px(pk - 1, 7, 1);
                    }
                    self.px(19, 10, 1);
                    self.bmp(17, 7, &[".###.", "#...#"]);
                    if pk >= 14 || ((t * 2.5) as isize).rem_euclid(2) == 0 {
                        self.bmp(16, 4, &[".#####.", "#.....#"]);
                    }
                }
            }
            "bt_speaker" => {
                self.bmp(1, 3, E_BT_SMALL);
                let kick = (t * 5.2).sin().max(0.0).powi(4);
                let snare = (t * 2.6 + 1.57).sin().max(0.0).powi(6);
                let master = self.voice_level.unwrap_or(1.0);
                for i in 0..E_EQ_COLS {
                    let n = (i as f64) / ((E_EQ_COLS - 1) as f64);
                    let band = if let Some(ref eq) = self.eq {
                        let idx = (i * eq.len() / E_EQ_COLS).min(eq.len() - 1);
                        eq[idx].clamp(0.0, 1.0)
                    } else if n < 0.35 {
                        0.20 + 0.80 * kick + 0.14 * (t * 7.5 + (i as f64) * 0.7).sin()
                    } else if n < 0.72 {
                        0.16 + 0.60 * (((t * 7.2 + (i as f64) * 0.75).sin() + (t * 4.5 - (i as f64) * 0.5).cos() + 2.0) * 0.25) + 0.24 * snare
                    } else {
                        0.10 + 0.68 * (((t * 12.0 + (i as f64)).cos() + (t * 8.5 + (i as f64) * 0.6).sin() + 2.0) * 0.25)
                    };
                    let sc = (band * (1.0 - 0.28 * (n * 2.0 - 1.0).powi(2)) * master).clamp(0.0, 1.0);
                    let tg = if sc < 0.05 { 1.0 } else { (1.0 + sc * 6.0).clamp(1.0, 7.0) };
                    let alpha = if tg > self.smooth[i] { 0.68 } else { 0.30 };
                    self.smooth[i] += (tg - self.smooth[i]) * alpha;
                    let half = rnd(self.smooth[i]).max(1);
                    self.peaks[i] = if (half as f64) >= self.peaks[i] {
                        half as f64
                    } else {
                        (self.peaks[i] - 0.22).max(half as f64)
                    };
                    let bx = 7 + i as isize;
                    for k in 0..half {
                        self.px(bx, 7 - k, 1);
                        self.px(bx, 8 + k, 1);
                    }
                    let pk = rnd(self.peaks[i]);
                    if pk > half {
                        if 7 - pk >= 0 {
                            self.px(bx, 7 - pk, 1);
                        }
                        if 8 + pk < E_H {
                            self.px(bx, 8 + pk, 1);
                        }
                    }
                }
            }
            "bt_on" => {
                self.bmp(9, 2, E_BT_BIG);
                let w = ((t * 3.5) as isize).rem_euclid(3);
                if w >= 1 {
                    self.bmp(5, 5, &[".#", "#.", "#.", "#.", ".#"]);
                    self.bmp(18, 5, &["#.", ".#", ".#", ".#", "#."]);
                }
                if w >= 2 {
                    self.bmp(2, 3, &[".#", "#.", "#.", "#.", "#.", "#.", "#.", "#.", ".#"]);
                    self.bmp(21, 3, &["#.", ".#", ".#", ".#", ".#", ".#", ".#", ".#", "#."]);
                }
            }
            "mic_muted" => {
                self.bmp(9, 3, E_MIC);
                if ((t * 2.5) as isize).rem_euclid(4) != 3 {
                    self.line(5, 13, 19, 2, 1);
                    self.line(6, 13, 20, 2, 1);
                }
            }
            "volume" => {
                let tg = self.volume.clamp(0, 100) as f64;
                if (tg - self.disp_vol).abs() < 1.0 {
                    self.disp_vol = tg;
                } else {
                    self.disp_vol += (tg - self.disp_vol) * 0.35;
                }
                let vol = rnd(self.disp_vol).clamp(0, 100);
                self.bmp(2, 2, E_SPEAKER);
                if vol == 0 {
                    self.line(7, 4, 9, 6, 1);
                    self.line(9, 4, 7, 6, 1);
                }
                let s = vol.to_string();
                let sx = 23 - (s.len() as isize * 4 - 1);
                for (i, ch) in s.chars().enumerate() {
                    self.bmp(sx + (i as isize) * 4, 3, dig3(ch));
                }
                self.line(2, 12, 22, 12, 1);
                self.line(2, 14, 22, 14, 1);
                self.px(2, 13, 1);
                self.px(22, 13, 1);
                let fill = rnd((vol as f64) / 100.0 * 19.0);
                if fill > 0 {
                    self.line(3, 13, 3 + fill - 1, 13, 1);
                }
            }
            "timer" => {
                let off = if ((t * 12.0) as isize).rem_euclid(2) == 0 { -1 } else { 1 };
                self.bmp(9 + off, 4, E_BELL);
                if ((t * 6.0) as isize).rem_euclid(2) == 0 {
                    self.bmp(4, 5, &[".#", "#.", "#.", ".#"]);
                    self.bmp(19, 5, &["#.", ".#", ".#", "#."]);
                }
            }
            "weather" => {
                self.bmp(4, 4, E_SUN);
                if ((t * 3.0) as isize).rem_euclid(2) == 0 {
                    for (px, py) in [(6, 2), (6, 10), (2, 6), (10, 6)] {
                        self.px(px, py, 1);
                    }
                } else {
                    for (px, py) in [(3, 3), (9, 3), (3, 9), (9, 9)] {
                        self.px(px, py, 1);
                    }
                }
                self.bmp(14 + rnd((t * 1.5).sin()), 9, E_CLOUD);
            }
            "updating" => {
                let d = ((t * 5.0) as isize).rem_euclid(3);
                self.line(12, 2 + d, 12, 7 + d, 1);
                self.line(10, 5 + d, 12, 7 + d, 1);
                self.line(14, 5 + d, 12, 7 + d, 1);
                self.line(3, 12, 21, 12, 1);
                self.line(3, 14, 21, 14, 1);
                self.px(3, 13, 1);
                self.px(21, 13, 1);
                let fill = 1 + ((t * 6.0) as isize).rem_euclid(17);
                self.line(4, 13, 4 + fill - 1, 13, 1);
            }
            _ => {}
        }
    }

    pub fn frame_rgb888(&self, brightness: u8) -> Vec<u8> {
        let mut out = vec![0u8; SCR_SIZE];
        for y in 0..SCR_H {
            for x in 0..SCR_W {
                if self.pixels[y][x] != 0 {
                    let o = (y * SCR_W + x) * SCR_BPP;
                    out[o] = brightness;
                    out[o + 1] = brightness;
                    out[o + 2] = brightness;
                }
            }
        }
        out
    }
}

fn resolve_eye_state(raw: &str) -> Option<(&'static str, Option<&'static str>, Option<&'static str>, Option<(isize, isize)>)> {
    let low = raw.trim().to_ascii_lowercase();
    let (mut name, variant) = match low.split_once(':') {
        Some((n, v)) => (n, Some(v)),
        None => (low.as_str(), None),
    };
    let mut out_variant = variant;
    if name == "variant" {
        if let Some(v) = variant {
            name = "speaking";
            out_variant = Some(v);
        }
    }
    let special = match name {
        "phone_connected" => Some("phone_connected"),
        "wake" => Some("wake"),
        _ => None,
    };
    let gaze = match name {
        "left" => Some((-1, 0)),
        "right" => Some((1, 0)),
        "up" => Some((0, -1)),
        "down" => Some((0, 1)),
        _ => None,
    };
    let canonical = match name {
        "open" | "blink" | "left" | "right" | "up" | "down" | "look" | "animate" | "wake" | "idle" => "idle",
        "listening" => "listening",
        "thinking" => "thinking",
        "speaking" => "speaking",
        "smile" | "happy" => "happy",
        "heart" | "hearts" | "love" => "love",
        "wide" | "surprised" => "surprised",
        "wink" => "wink",
        "squint" | "cool" => "cool",
        "dizzy" | "confused" => "confused",
        "angry" => "angry",
        "cry" | "sad" => "sad",
        "sleeping" | "phone_connected" | "sleep" => "sleep",
        "boot" => "boot",
        "setup" => "setup",
        "bt" | "bt_speaker" => "bt_speaker",
        "bt_on" => "bt_on",
        "volume" => "volume",
        "timer" => "timer",
        "weather" => "weather",
        "muted" | "mic_muted" => "mic_muted",
        "update" | "updating" => "updating",
        _ => return None,
    };
    let v = out_variant.map(|s| match s {
        "happy" => "happy",
        "angry" => "angry",
        "sad" => "sad",
        "surprised" => "surprised",
        _ => "default",
    });
    Some((canonical, v, special, gaze))
}

fn poll_eyes_udp(
    sock: Option<&UdpSocket>,
    eyes: &mut Eyes,
    now: f64,
    now_s: f64,
    lvl_ts: &mut f64,
    eq_ts: &mut f64,
) {
    let Some(s) = sock else { return };
    let mut buf = [0u8; 512];
    loop {
        let Ok(n) = s.recv(&mut buf) else { break };
        let text = String::from_utf8_lossy(&buf[..n]);
        for line in text.lines() {
            let cmd = line.trim().to_ascii_lowercase();
            if let Some((k, arg)) = cmd.split_once(':') {
                let k = k.trim();
                let arg = arg.trim();
                if k == "level" {
                    if matches!(arg, "none" | "off" | "clear" | "") {
                        eyes.voice_level = None;
                        *lvl_ts = 0.0;
                    } else if let Ok(v) = arg.parse::<f64>() {
                        eyes.voice_level = Some(v.clamp(0.0, 1.0));
                        *lvl_ts = now_s;
                    }
                } else if k == "eq" {
                    if matches!(arg, "none" | "off" | "clear" | "") {
                        eyes.eq = None;
                        *eq_ts = 0.0;
                    } else {
                        let bands: Vec<f64> = arg
                            .split(',')
                            .filter_map(|x| x.trim().parse::<f64>().ok())
                            .map(|x| x.clamp(0.0, 1.0))
                            .collect();
                        if !bands.is_empty() {
                            eyes.eq = Some(bands);
                            *eq_ts = now_s;
                        }
                    }
                } else if k == "speaking" || k == "variant" {
                    let var = if arg.is_empty() { "default" } else { arg };
                    eyes.set_state("speaking", Some(var), now);
                } else if k == "state" || k == "eyes" {
                    if let Some((name, var, _, _)) = resolve_eye_state(arg) {
                        let cur_v = if name == "speaking" { var.unwrap_or("default") } else { "" };
                        if eyes.state != name || (name == "speaking" && eyes.variant != cur_v) {
                            eyes.set_state(name, var, now);
                        }
                    }
                }
            }
        }
    }
}

fn poll_voice_level_file(eyes: &mut Eyes, now_s: f64, lvl_ts: &mut f64, last_mtime: &mut Option<SystemTime>) {
    if *lvl_ts > 0.0 && (now_s - *lvl_ts) <= 0.15 {
        return;
    }
    let Ok(meta) = fs::metadata(VOICE_LEVEL_FILE) else { return };
    let Ok(mtime) = meta.modified() else { return };
    let age_ok = SystemTime::now().duration_since(mtime).map(|d| d.as_secs_f64() < 0.45).unwrap_or(false);
    if !age_ok || Some(mtime) == *last_mtime {
        return;
    }
    if let Ok(raw) = fs::read_to_string(VOICE_LEVEL_FILE) {
        *last_mtime = Some(mtime);
        let low = raw.trim().to_ascii_lowercase();
        if matches!(low.as_str(), "" | "none" | "off" | "clear") {
            eyes.voice_level = None;
            *lvl_ts = 0.0;
        } else if let Ok(v) = low.parse::<f64>() {
            eyes.voice_level = Some(v.clamp(0.0, 1.0));
            *lvl_ts = now_s;
        }
    }
}

pub fn screen_eyes(state: &str, brightness: u8, duration: Option<f64>) {
    let Some((name, variant, special, gaze)) = resolve_eye_state(state) else {
        eprintln!("[Screen] Unknown eyes state '{state}'.");
        return;
    };
    let start = Instant::now();
    let ms_now = || start.elapsed().as_secs_f64() * 1000.0;
    let now0 = ms_now();
    let mut eyes = Eyes::new(now0);
    if name == "volume" {
        if let Ok(s) = fs::read_to_string("/var/lib/atrios/volume") {
            if let Ok(v) = s.trim().parse::<i32>() {
                eyes.volume = v;
            }
        }
    }
    eyes.set_state(name, variant, now0);
    if special == Some("phone_connected") {
        eyes.sleep_mode = "none";
    } else if special == Some("wake") {
        eyes.sleep_mode = "waking";
        eyes.sleep_start = now0;
    }
    if let Some((gx, gy)) = gaze {
        eyes.manual_gaze = true;
        eyes.tdx = gx;
        eyes.tdy = gy;
    }

    let sock = UdpSocket::bind(EYES_UDP_ADDR).ok();
    if let Some(ref s) = sock {
        let _ = s.set_nonblocking(true);
    }
    let mut lvl_ts = 0.0;
    let mut eq_ts = 0.0;
    let mut file_mtime: Option<SystemTime> = None;

    println!(
        "[Screen] Eyes: {}{} (brightness={brightness}/255)",
        name,
        if name == "speaking" {
            format!(":{}", eyes.variant)
        } else {
            String::new()
        }
    );

    let deadline = duration.map(|d| start + Duration::from_secs_f64(d));
    while deadline.map(|d| Instant::now() < d).unwrap_or(true) {
        let now = ms_now();
        let now_s = now / 1000.0;
        poll_eyes_udp(sock.as_ref(), &mut eyes, now, now_s, &mut lvl_ts, &mut eq_ts);
        poll_voice_level_file(&mut eyes, now_s, &mut lvl_ts, &mut file_mtime);
        if eyes.voice_level.is_some() && lvl_ts > 0.0 && (now_s - lvl_ts) > 0.45 {
            eyes.voice_level = None;
        }
        if eyes.eq.is_some() && eq_ts > 0.0 && (now_s - eq_ts) > 0.6 {
            eyes.eq = None;
        }
        eyes.render(now);
        screen_send_frame(&eyes.frame_rgb888(brightness));

        if eyes.state == "boot" && eyes.boot_done(now) {
            let sys_boot = fs::read_to_string(SCREEN_MODE_FILE)
                .map(|s| s.trim() == "boot")
                .unwrap_or(false);
            if sys_boot {
                let _ = fs::create_dir_all("/var/run/atrios");
                let _ = fs::write(SCREEN_MODE_FILE, "clock");
                let hm = format_local("%H:%M", None);
                let clk = render_clock_frame(&hm, is_bt_active(), brightness);
                screen_send_frame(&clk);
                break;
            } else {
                eyes.set_state("idle", None, now);
            }
        }
        thread::sleep(Duration::from_millis(1000 / E_FPS));
    }
}

pub fn resolve_error_variant(raw: &str) -> &'static str {
    let low = raw.trim().to_ascii_lowercase();
    let k = low.strip_prefix("error:").unwrap_or(&low);
    let k = if k.is_empty() { "default" } else { k };
    match k {
        "wifi" | "network" => "wifi",
        "no_internet" | "internet" | "tls" | "server" => "no_internet",
        "overheat" | "thermal" => "overheat",
        "audio_err" | "audio" => "audio_err",
        "update" | "ota" => "update",
        "kernel_panic" | "panic" | "storage" => "kernel_panic",
        _ => "default",
    }
}

fn err_bmp(buf: &mut [u8], x0: isize, y0: isize, rows: &[&str], bright: u8) {
    for (r, row) in rows.iter().enumerate() {
        for (c, ch) in row.chars().enumerate() {
            if ch == '#' {
                screen_set_pixel(buf, x0 + c as isize, y0 + r as isize, bright);
            }
        }
    }
}

fn err_rect(buf: &mut [u8], x0: isize, y0: isize, w: isize, h: isize, bright: u8) {
    for dy in 0..h {
        for dx in 0..w {
            screen_set_pixel(buf, x0 + dx, y0 + dy, bright);
        }
    }
}

pub fn render_error_frame(variant: &str, t: f64, bright: u8) -> Vec<u8> {
    let mut buf = vec![0u8; SCR_SIZE];
    let v = resolve_error_variant(variant);
    match v {
        "wifi" => {
            err_bmp(&mut buf, 2, 4, E_WIFI, bright);
            if ((t * 3.5) as isize).rem_euclid(2) == 0 {
                err_bmp(&mut buf, 16, 6, E_X_SMALL, bright);
            }
        }
        "no_internet" => {
            err_bmp(&mut buf, 3, 4, E_GLOBE, bright);
            if ((t * 3.5) as isize).rem_euclid(2) == 0 {
                err_rect(&mut buf, 17, 4, 2, 5, bright);
                err_rect(&mut buf, 17, 10, 2, 2, bright);
            }
        }
        "overheat" => {
            err_bmp(&mut buf, 6, 4, E_THERM, bright);
            if ((t * 4.0) as isize).rem_euclid(2) == 0 {
                err_rect(&mut buf, 15, 4, 2, 5, bright);
                err_rect(&mut buf, 15, 10, 2, 2, bright);
            }
        }
        "audio_err" => {
            err_bmp(&mut buf, 5, 4, E_SPEAKER, bright);
            if ((t * 3.5) as isize).rem_euclid(2) == 0 {
                err_bmp(&mut buf, 13, 5, E_X_SMALL, bright);
            }
        }
        "update" => {
            // Animated downward update/download arrow into tray + blinking X
            const E_DL_ARROW: &[&str] = &[
                "..###..",
                "..###..",
                "..###..",
                "#######",
                ".#####.",
                "..###..",
                "...#...",
            ];
            let bob = ((t * 3.0) as isize).rem_euclid(2);
            err_bmp(&mut buf, 3, 2 + bob, E_DL_ARROW, bright);
            // Download tray bar at bottom
            err_rect(&mut buf, 2, 11, 9, 2, bright);
            if ((t * 3.5) as isize).rem_euclid(2) == 0 {
                err_bmp(&mut buf, 15, 5, E_X_SMALL, bright);
            }
        }
        "kernel_panic" => {
            let glitch = ((t * 18.0) as isize).rem_euclid(3) - 1;
            err_bmp(&mut buf, E_LEFT_X + glitch, 2, E_X_SPRITE, bright);
            err_bmp(&mut buf, E_RIGHT_X - glitch, 2, E_X_SPRITE, bright);
            let seed = (t * 20.0) as isize;
            for x in 0..E_W {
                if ((x * 13 + seed * 7).rem_euclid(11)) < 3 {
                    screen_set_pixel(&mut buf, x, 12 + (x + seed).rem_euclid(3), bright);
                }
            }
            if seed.rem_euclid(4) == 0 {
                let y_line = (seed * 3).rem_euclid(E_H);
                for x in 0..E_W {
                    screen_set_pixel(&mut buf, x, y_line, bright);
                }
            }
        }
        _ => {
            let shake = if ((t * 16.0) as isize).rem_euclid(2) == 0 {
                0
            } else if ((t * 8.0) as isize).rem_euclid(2) == 0 {
                1
            } else {
                -1
            };
            if ((t * 5.0) as isize).rem_euclid(4) != 3 {
                err_bmp(&mut buf, E_LEFT_X + shake, 4, E_X_SPRITE, bright);
                err_bmp(&mut buf, E_RIGHT_X + shake, 4, E_X_SPRITE, bright);
            }
        }
    }
    buf
}

fn screen_error(variant: &str, brightness: u8) {
    let v = resolve_error_variant(variant);
    println!("[Screen] Error: {v} (brightness={brightness}/255)");
    let t0 = Instant::now();
    loop {
        let buf = render_error_frame(v, t0.elapsed().as_secs_f64(), brightness);
        screen_send_frame(&buf);
        thread::sleep(Duration::from_millis(1000 / E_FPS));
    }
}

fn screen_ticker(text: &str, brightness: u8) {
    let s = format!(" {} ", text.to_ascii_uppercase());
    let chars: Vec<char> = s.chars().collect();
    let total_cols = chars.len() * 6;
    let mut grid = vec![vec![0u8; total_cols]; 7];
    for (ci, &ch) in chars.iter().enumerate() {
        let glyph = font_5x7(ch);
        for (r, &bits) in glyph.iter().enumerate() {
            for c in 0..5usize {
                if ((bits >> (4 - c)) & 1) != 0 {
                    grid[r][ci * 6 + c] = 1;
                }
            }
        }
    }
    println!("[Screen] Scrolling: \"{}\"...", text);
    for offset in 0..=total_cols.saturating_sub(SCR_W) {
        let mut buf = vec![0u8; SCR_SIZE];
        for r in 0..7 {
            for x in 0..SCR_W {
                if grid[r][offset + x] != 0 {
                    screen_set_pixel(&mut buf, x as isize, (4 + r) as isize, brightness);
                }
            }
        }
        screen_send_frame(&buf);
        thread::sleep(Duration::from_millis(70));
    }
    screen_send_frame(&vec![0u8; SCR_SIZE]);
}

fn ring_fill(r: u8, g: u8, b: u8) {
    let mut frame = Vec::with_capacity(RING_SIZE);
    for _ in 0..RING_LEDS {
        frame.extend_from_slice(&[r, g, b]);
    }
    ring_send_frame(&frame);
    println!("[Ring] Filled RGB({r}, {g}, {b}).");
}

fn ring_amber(duration: f64) {
    println!("[Ring] Running amber shimmer animation...");
    let start = Instant::now();
    let mut phase = 0.0f64;
    while duration <= 0.0 || start.elapsed().as_secs_f64() < duration {
        if Path::new(RING_OVERRIDE_FILE).exists() {
            return;
        }
        let mut frame = vec![0u8; RING_SIZE];
        for i in 0..RING_LEDS {
            let wave = 0.5 * (1.0 + (phase + (i as f64) * (2.0 * std::f64::consts::PI / RING_LEDS as f64) * 2.0).sin());
            frame[i * 3] = (140.0 + 115.0 * wave) as u8;
            frame[i * 3 + 1] = (35.0 + 125.0 * wave) as u8;
            frame[i * 3 + 2] = (14.0 * wave * wave) as u8;
        }
        ring_send_frame(&frame);
        phase = (phase + 0.16) % (2.0 * std::f64::consts::PI);
        thread::sleep(Duration::from_millis(40));
    }
    ring_send_frame(&vec![0u8; RING_SIZE]);
}

fn ring_amber_green(duration: f64) {
    println!("[Ring] Running amber+green runner animation...");
    let start = Instant::now();
    let mut pos = 0.0f64;
    while duration <= 0.0 || start.elapsed().as_secs_f64() < duration {
        if Path::new(RING_OVERRIDE_FILE).exists() {
            return;
        }
        let mut frame = vec![0u8; RING_SIZE];
        for i in 0..RING_LEDS {
            let d = ((i as f64) - pos).abs().min((RING_LEDS as f64) - ((i as f64) - pos).abs());
            let gm = (1.0 - d / 3.2).max(0.0).powi(2);
            frame[i * 3] = (225.0 * (1.0 - gm)) as u8;
            frame[i * 3 + 1] = (90.0 * (1.0 - gm) + 255.0 * gm) as u8;
            frame[i * 3 + 2] = (20.0 * gm) as u8;
        }
        ring_send_frame(&frame);
        pos = (pos + 0.45) % (RING_LEDS as f64);
        thread::sleep(Duration::from_millis(40));
    }
    ring_send_frame(&vec![0u8; RING_SIZE]);
}

fn ring_rainbow() {
    println!("[Ring] Running rainbow animation...");
    let start = Instant::now();
    let mut step = 0i32;
    while start.elapsed().as_secs_f64() < 5.0 {
        let mut frame = vec![0u8; RING_SIZE];
        for i in 0..RING_LEDS {
            let hue = (step + (i as i32) * (360 / RING_LEDS as i32)) % 360;
            let c = 255.0f64;
            let x = (c * (1.0 - (((hue as f64) / 60.0) % 2.0 - 1.0).abs())) as u8;
            let (r, g, b) = if hue < 60 { (255, x, 0) }
                else if hue < 120 { (x, 255, 0) }
                else if hue < 180 { (0, 255, x) }
                else if hue < 240 { (0, x, 255) }
                else if hue < 300 { (x, 0, 255) }
                else { (255, 0, x) };
            frame[i * 3] = r;
            frame[i * 3 + 1] = g;
            frame[i * 3 + 2] = b;
        }
        ring_send_frame(&frame);
        step = (step + 10) % 360;
        thread::sleep(Duration::from_millis(50));
    }
    ring_send_frame(&vec![0u8; RING_SIZE]);
}

fn ring_volume(pct: i32) {
    let pct = pct.clamp(0, 100);
    let active = (((RING_LEDS as f64) * (pct as f64 / 100.0)).round() as usize).min(RING_LEDS);
    let mut frame = vec![0u8; RING_SIZE];
    let red_adv = (((pct as f64) - 50.0) / 50.0).clamp(0.0, 1.0);
    let red_thr = 1.0 - red_adv;
    for i in 0..active {
        let ratio = (i as f64) / ((RING_LEDS - 1).max(1) as f64);
        let (r, g) = if ratio >= red_thr {
            (255u8, 0u8)
        } else {
            let rel = ratio / red_thr.max(0.01);
            if rel < 0.5 { ((255.0 * rel * 2.0) as u8, 255) } else { (255, (255.0 * (2.0 - rel * 2.0)) as u8) }
        };
        frame[i * 3] = r;
        frame[i * 3 + 1] = g;
    }
    ring_send_frame(&frame);
    println!("[Ring] Volume arc: {pct}% ({active}/{RING_LEDS} LEDs lit).");
    if active > 0 {
        thread::sleep(Duration::from_secs(1));
        for s in 1..=15 {
            let prog = (s as f64) / 15.0;
            let b = 0.5 * (1.0 + (std::f64::consts::PI * prog).cos());
            let dimmed: Vec<u8> = frame.iter().map(|&v| ((v as f64) * b) as u8).collect();
            ring_send_frame(&dimmed);
            thread::sleep(Duration::from_millis(40));
        }
        ring_send_frame(&vec![0u8; RING_SIZE]);
    }
}

pub fn read_sensor_lux() -> Option<f64> {
    let p = find_lux_sensor()?;
    let raw = fs::read_to_string(p).ok()?.trim().parse::<f64>().ok()?;
    Some(raw * 0.2)
}

pub fn run(args: &[String]) -> i32 {
    let mut brightness: u8 = 220;
    let mut pos = Vec::new();
    let mut i = 0;
    while i < args.len() {
        if args[i] == "--brightness" && i + 1 < args.len() {
            brightness = args[i + 1].parse::<i32>().unwrap_or(220).clamp(0, 255) as u8;
            i += 2;
        } else {
            pos.push(args[i].as_str());
            i += 1;
        }
    }
    let comp = pos.first().copied().unwrap_or("");
    match comp {
        "sensor" => {
            if let Some(lux) = read_sensor_lux() {
                println!("[Sensor] Ambient Light (LTR-308ALS): {lux:.1} Lux");
            } else {
                println!("[Sensor] LTR-308ALS not found on sysfs.");
            }
        }
        "ring" => {
            let act = pos.get(1).copied().unwrap_or("rainbow");
            let vals = pos.get(2..).unwrap_or(&[]);
            match act {
                "off" | "clear" => ring_fill(0, 0, 0),
                "red" => ring_fill(255, 0, 0),
                "blue" => ring_fill(0, 0, 255),
                "green" => ring_fill(0, 255, 0),
                "rainbow" => ring_rainbow(),
                "amber" => ring_amber(vals.first().and_then(|s| s.parse().ok()).unwrap_or(8.0)),
                "amber_green" => ring_amber_green(vals.first().and_then(|s| s.parse().ok()).unwrap_or(15.0)),
                "volume" => ring_volume(vals.first().and_then(|s| s.parse().ok()).unwrap_or(50)),
                "fill" => ring_fill(
                    vals.first().and_then(|s| s.parse().ok()).unwrap_or(255),
                    vals.get(1).and_then(|s| s.parse().ok()).unwrap_or(255),
                    vals.get(2).and_then(|s| s.parse().ok()).unwrap_or(255),
                ),
                _ => {
                    eprintln!("Unknown ring action: {act}");
                    return 1;
                }
            }
        }
        "screen" => {
            let mut act = pos.get(1).copied().unwrap_or("clock");
            let mut vals = pos.get(2..).unwrap_or(&[]).to_vec();
            let err_rest: String;
            if let Some(rest) = act.to_ascii_lowercase().strip_prefix("error:") {
                act = "error";
                err_rest = if rest.is_empty() { "default".into() } else { rest.to_string() };
                vals.insert(0, err_rest.as_str());
                return run_screen_action(act, &vals, brightness);
            }
            return run_screen_action(act, &vals, brightness);
        }
        _ => {
            println!("AtriOS Light & Display Controller (Rust)");
        }
    }
    0
}

fn run_screen_action(act: &str, vals: &[&str], b: u8) -> i32 {
    match act {
        "clear" | "off" => {
            screen_send_frame(&vec![0u8; SCR_SIZE]);
            println!("[Screen] Cleared (All LEDs OFF).");
        }
        "fill" => {
            let v = vals.first().and_then(|s| s.parse::<i32>().ok()).unwrap_or(b as i32).clamp(0, 255) as u8;
            let mut buf = vec![0u8; SCR_SIZE];
            for y in 0..SCR_H {
                for x in 0..SCR_W {
                    screen_set_pixel(&mut buf, x as isize, y as isize, v);
                }
            }
            screen_send_frame(&buf);
        }
        "clock" => screen_clock(b),
        "kisser" | "cat" | "boykisser" => screen_boykisser(vals.first().copied().unwrap_or("animate"), b),
        "eyes" | "smile" | "happy" => {
            let st = if matches!(act, "smile" | "happy") {
                "happy"
            } else {
                vals.first().copied().unwrap_or("idle")
            };
            screen_eyes(st, b, None);
        }
        "error" => screen_error(vals.first().copied().unwrap_or("default"), b),
        "ticker" => screen_ticker(&if vals.is_empty() { "ATRIOS".into() } else { vals.join(" ") }, b),
        "pixel" => {
            if vals.len() >= 2 {
                let x: isize = vals[0].parse().unwrap_or(0);
                let y: isize = vals[1].parse().unwrap_or(0);
                let v = vals.get(2).and_then(|s| s.parse::<i32>().ok()).unwrap_or(b as i32).clamp(0, 255) as u8;
                let mut buf = vec![0u8; SCR_SIZE];
                screen_set_pixel(&mut buf, x, y, v);
                screen_send_frame(&buf);
            } else {
                eprintln!("Usage: atrios screen pixel <x> <y> [brightness]");
                return 1;
            }
        }
        "gradient" => {
            let mut buf = vec![0u8; SCR_SIZE];
            for y in 0..SCR_H {
                for x in 0..SCR_W {
                    let v = (255.0 * (x as f64 / (SCR_W - 1) as f64) * (y as f64 / (SCR_H - 1) as f64)) as u8;
                    screen_set_pixel(&mut buf, x as isize, y as isize, v);
                }
            }
            screen_send_frame(&buf);
        }
        "test" => {
            let mut buf = vec![100u8; SCR_SIZE];
            screen_send_frame(&buf);
            thread::sleep(Duration::from_millis(800));
            screen_eyes("happy", b, Some(1.5));
            screen_ticker("ATRIOS", b);
            screen_clock(b);
            let _ = &mut buf;
        }
        _ => {
            eprintln!("Unknown screen action: {act}");
            return 1;
        }
    }
    0
}
