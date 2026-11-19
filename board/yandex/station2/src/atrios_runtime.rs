#[allow(dead_code)]
#[path = "atrios_runtime/sys.rs"]
mod sys;
#[path = "atrios_runtime/json.rs"]
mod json;
#[path = "atrios_runtime/status.rs"]
mod status;
#[path = "atrios_runtime/https.rs"]
mod https;
#[path = "atrios_runtime/light.rs"]
mod light;
#[path = "atrios_runtime/screend.rs"]
mod screend;
#[path = "atrios_runtime/rotaryd.rs"]
mod rotaryd;
#[path = "atrios_runtime/atriai.rs"]
mod atriai;
#[path = "atrios_runtime/bt_control.rs"]
mod bt_control;
#[path = "atrios_runtime/ota.rs"]
mod ota;
#[path = "atrios_runtime/tools.rs"]
mod tools;

use std::path::Path;

fn applet_name(argv0: &str) -> &str {
    Path::new(argv0)
        .file_name()
        .and_then(|s| s.to_str())
        .unwrap_or(argv0)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut applet = args.first().map(|s| applet_name(s)).unwrap_or("atrios-runtime");
    let mut rest: &[String] = if args.len() > 1 { &args[1..] } else { &[] };

    if matches!(applet, "atrios-runtime" | "atrios_runtime") {
        if let Some(first) = rest.first() {
            applet = first.as_str();
            rest = &rest[1..];
        } else {
            eprintln!("Usage: atrios-runtime <light|screend|rotaryd|atriai|bt-control|ota|ntp|micprobe|bt-agent|bt-volume-sync|rtk-bt-bridge|rtk-bt-init|wifi-scan> [args]");
            std::process::exit(1);
        }
    }

    let code = match applet {
        "light" => light::run(rest),
        "screend" => screend::run(rest),
        "rotaryd" => rotaryd::run(rest),
        "atriai" => atriai::run(rest),
        "bt-control" => bt_control::run(rest),
        "ota" => ota::run(rest),
        "ntp" => tools::run_ntp(rest),
        "micprobe" => tools::run_micprobe(rest),
        "bt-agent" => tools::run_bt_agent(rest),
        "bt-volume-sync" => tools::run_bt_volume_sync(rest),
        "rtk-bt-bridge" => tools::run_rtk_bt_bridge(rest),
        "rtk-bt-init" => tools::run_rtk_bt_init(rest),
        "wifi-scan" | "wifi_scan" => tools::run_wifi_scan(rest),
        _ => {
            eprintln!("Unknown applet: {applet}");
            1
        }
    };
    std::process::exit(code);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn json_roundtrip_and_escapes() {
        let j = json::Json::parse(r#"{"a":"привет\n\"Atri\"","b":true,"n":42}"#).unwrap();
        assert_eq!(j.get_str("a"), Some("привет\n\"Atri\""));
        assert_eq!(j.get("b").and_then(|v| v.as_bool()), Some(true));
        assert_eq!(j.get("n").and_then(|v| v.as_i64()), Some(42));
    }

    #[test]
    fn wake_word_boundaries() {
        assert!(atriai::matches_wake_word("Atri, привет!", "Atri"));
        assert!(atriai::matches_wake_word("эй атри, сделай громче", "Atri"));
        assert!(atriai::matches_wake_word("три какая погода", "Atri"));
        assert!(atriai::matches_wake_word("три", "Atri"));
        assert!(atriai::matches_wake_word("матри", "Atri"));
        assert!(atriai::matches_wake_word("мотри", "Atri"));
        assert!(atriai::matches_wake_word("дизайнератри", "Atri"));
        assert!(!atriai::matches_wake_word("смотри", "Atri"));
        assert!(!atriai::matches_wake_word("atrium hello", "Atri"));
        assert!(!atriai::matches_wake_word("patriarch", "Atri"));
        assert_eq!(atriai::strip_wake_word("Эй, Атри, громче", "Atri"), "громче");
        assert_eq!(atriai::strip_wake_word("три", "Atri"), "");
        assert_eq!(atriai::strip_wake_word("дизайнератри", "Atri"), "");
        assert_eq!(atriai::strip_wake_word("громче атри", "Atri"), "громче");
    }

    #[test]
    fn boot_eyes_collapse_completely_before_done() {
        let mut e = light::Eyes::new(0.0);
        e.set_state("boot", None, 0.0);
        // Step through beam, orbit end latch at 2.9s, then open eyes, collapse, done:
        e.render(500.0);
        assert!(!e.boot_done(500.0));
        e.render(2900.0); // oe = 2.90
        e.render(4700.0); // post = 1.80, sub = 1.10 -> open eyes (after double-blink)
        let lit_open: usize = e.pixels.iter().flatten().filter(|&&p| p != 0).count();
        assert!(lit_open > 60, "expected eyes open, got {lit_open} px");
        assert!(!e.boot_done(4700.0));

        // Vertical + horizontal collapse at sub = 1.82 (t = 2900 + 700 + 1820 = 5420)
        e.render(5420.0);
        let lit_slit: usize = e.pixels.iter().flatten().filter(|&&p| p != 0).count();
        assert!(lit_slit < lit_open && lit_slit <= 14, "expected collapsing slit, got {lit_slit}");

        // Completely collapsed (0 pixels lit) at sub = 1.95 (t = 5550) before clock starts
        e.render(5550.0);
        let lit_closed: usize = e.pixels.iter().flatten().filter(|&&p| p != 0).count();
        assert_eq!(lit_closed, 0, "expected eyes fully collapsed to 0 px");
        assert!(!e.boot_done(5550.0));

        // Done at post >= 2.70 (t >= 5600)
        e.render(5650.0);
        assert!(e.boot_done(5650.0));
    }

    #[test]
    fn volume_curves_match_expected_range() {
        assert_eq!(rotaryd::volume_to_raw(0, 255), 0);
        assert_eq!(rotaryd::volume_to_raw(100, 255), 255);
        assert!(rotaryd::woofer_volume_to_raw(10, 255) >= rotaryd::volume_to_raw(10, 255));
    }

    #[test]
    fn sha256_known_vector() {
        let digest = sys::hex_lower(&sys::sha256_bytes(b"abc"));
        assert_eq!(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }
}
