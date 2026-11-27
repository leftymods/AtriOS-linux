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
#[cfg(not(atrios_no_display))]
#[path = "atrios_runtime/screend.rs"]
mod screend;
#[path = "atrios_runtime/rotaryd.rs"]
mod rotaryd;
#[path = "atrios_runtime/local_stt.rs"]
mod local_stt;
#[path = "atrios_runtime/local_tts.rs"]
mod local_tts;
#[path = "atrios_runtime/npu.rs"]
mod npu;
#[path = "atrios_runtime/local_ai.rs"]
mod local_ai;
#[path = "atrios_runtime/atriai.rs"]
mod atriai;
#[path = "atrios_runtime/bt_control.rs"]
mod bt_control;
#[path = "atrios_runtime/ota.rs"]
mod ota;
#[path = "atrios_runtime/ymusic.rs"]
mod ymusic;
#[path = "atrios_runtime/ynison.rs"]
mod ynison;
#[path = "atrios_runtime/hardware.rs"]
mod hardware;
#[path = "atrios_runtime/supervise.rs"]
mod supervise;
#[allow(dead_code)]
#[path = "atrios_runtime/e2ee.rs"]
mod e2ee;
#[path = "atrios_runtime/tools.rs"]
mod tools;

use std::path::Path;

fn applet_name(argv0: &str) -> &str {
    Path::new(argv0)
        .file_name()
        .and_then(|s| s.to_str())
        .unwrap_or(argv0)
}

fn runtime_flavor() -> &'static str {
    if cfg!(atrios_flavor_minimal) {
        "minimal"
    } else if cfg!(atrios_flavor_lite) {
        "lite"
    } else {
        "full"
    }
}

fn print_modules_json() {
    let mut applets: Vec<&'static str> = vec!["ntp", "micprobe", "wifi-scan", "hardware", "supervise"];
    if !cfg!(atrios_no_light) { applets.push("light"); }
    if !cfg!(atrios_no_display) { applets.push("screend"); }
    if !cfg!(atrios_no_rotary) { applets.push("rotaryd"); }
    if !cfg!(atrios_no_atriai) { applets.push("atriai"); }
    if !cfg!(atrios_no_bluetooth) {
        applets.extend_from_slice(&["bt-control", "bt-agent", "bt-volume-sync", "rtk-bt-bridge", "rtk-bt-init"]);
    }
    if !cfg!(atrios_no_ota) { applets.push("ota"); }
    if !cfg!(atrios_no_ymusic) { applets.push("ymusic"); }
    let list: Vec<String> = applets.iter().map(|a| format!("\"{}\"", a)).collect();
    println!("{{\"flavor\":\"{}\",\"applets\":[{}]}}", runtime_flavor(), list.join(","));
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut applet = args.first().map(|s| applet_name(s)).unwrap_or("atrios-runtime");
    let mut rest: &[String] = if args.len() > 1 { &args[1..] } else { &[] };

    if matches!(applet, "atrios-runtime" | "atrios_runtime") {
        if let Some(first) = rest.first() {
            if matches!(first.as_str(), "--modules" | "modules") {
                print_modules_json();
                std::process::exit(0);
            }
            applet = first.as_str();
            rest = &rest[1..];
        } else {
            eprintln!("Usage: atrios-runtime <modules|light|screend|rotaryd|atriai|bt-control|ota|ymusic|ntp|micprobe|bt-agent|bt-volume-sync|rtk-bt-bridge|rtk-bt-init|wifi-scan> [args]");
            std::process::exit(1);
        }
    }

    let code = match applet {
        #[cfg(not(atrios_no_light))]
        "light" => light::run(rest),
        #[cfg(not(atrios_no_display))]
        "screend" => screend::run(rest),
        #[cfg(not(atrios_no_rotary))]
        "rotaryd" => rotaryd::run(rest),
        #[cfg(not(atrios_no_atriai))]
        "atriai" => atriai::run(rest),
        #[cfg(not(atrios_no_bluetooth))]
        "bt-control" => bt_control::run(rest),
        #[cfg(not(atrios_no_ota))]
        "ota" => ota::run(rest),
        #[cfg(not(atrios_no_ymusic))]
        "ymusic" => ymusic::run(rest),
        "ntp" => tools::run_ntp(rest),
        "micprobe" => tools::run_micprobe(rest),
        #[cfg(not(atrios_no_bluetooth))]
        "bt-agent" => tools::run_bt_agent(rest),
        #[cfg(not(atrios_no_bluetooth))]
        "bt-volume-sync" => tools::run_bt_volume_sync(rest),
        #[cfg(not(atrios_no_bluetooth))]
        "rtk-bt-bridge" => tools::run_rtk_bt_bridge(rest),
        #[cfg(not(atrios_no_bluetooth))]
        "rtk-bt-init" => tools::run_rtk_bt_init(rest),
        "wifi-scan" | "wifi_scan" => tools::run_wifi_scan(rest),
        "hardware" => hardware::run(rest),
        "supervise" => supervise::run(rest),
        "modules" | "--modules" => {
            print_modules_json();
            0
        }
        _ => {
            eprintln!("Applet disabled or unknown in flavor '{}': {applet}", runtime_flavor());
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
        assert!(atriai::matches_wake_word("а три сделай громче", "Atri"));
        assert!(atriai::matches_wake_word("арти поставь таймер", "Atri"));
        assert!(atriai::matches_wake_word("громче атри", "Atri"));
        // Reject casual Russian phrases that caused false wakes
        assert!(!atriai::matches_wake_word("три", "Atri"));
        assert!(!atriai::matches_wake_word("три какая погода", "Atri"));
        assert!(!atriai::matches_wake_word("на три", "Atri"));
        assert!(!atriai::matches_wake_word("в три часа встретимся", "Atri"));
        assert!(!atriai::matches_wake_word("по три рубля", "Atri"));
        assert!(!atriai::matches_wake_word("за три дня", "Atri"));
        assert!(!atriai::matches_wake_word("я сегодня а три часа был дома", "Atri"));
        assert!(!atriai::matches_wake_word("утро доброе сегодня", "Atri"));
        assert!(!atriai::matches_wake_word("отряд пошёл вперёд", "Atri"));
        assert!(!atriai::matches_wake_word("смотри", "Atri"));
        assert!(!atriai::matches_wake_word("atrium hello", "Atri"));
        assert!(!atriai::matches_wake_word("атриум", "Atri"));
        assert!(!atriai::matches_wake_word("эй, атриум", "Атри"));
        assert!(!atriai::matches_wake_word("как найти атриум", "Atri"));
        assert!(!atriai::matches_wake_word("а три ум", "Atri"));
        assert!(!atriai::matches_wake_word("натрий", "Atri"));
        assert!(!atriai::matches_wake_word("астры", "Atri"));
        assert!(atriai::matches_wake_word("атриум", "атриум"));
        assert!(!atriai::matches_wake_word("patriarch", "Atri"));
        assert_eq!(atriai::strip_wake_word("Эй, Атри, громче", "Atri"), "громче");
        assert_eq!(atriai::strip_wake_word("а три а три", "Atri"), "");
        assert_eq!(atriai::strip_wake_word("атри атри как дела", "Atri"), "как дела");
        assert_eq!(atriai::strip_wake_word("громче атри", "Atri"), "громче");
    }

    #[test]
    fn voice_retry_preserves_wake_boundaries_and_command_pauses() {
        assert!(atriai::should_retry_wake("", true));
        assert!(atriai::should_retry_wake("три какая погода", false));
        assert!(atriai::should_retry_wake("поставь таймер", true));
        assert!(!atriai::should_retry_wake("атриум", true));
        assert!(!atriai::should_retry_wake("а три ум", true));
        assert!(!atriai::should_retry_wake("реклама", false));
        assert_eq!(atriai::voice_endpoint_silence_ms(true, 500.0, false, 420.0), 128.0);
        assert_eq!(atriai::voice_endpoint_silence_ms(false, 1800.0, false, 550.0), 350.0);
    }

    #[test]
    fn immediate_stop_requires_a_command_boundary() {
        for text in ["стоп", "Стоп!", "с топ", "Атри, стоп", "stop пожалуйста"] {
            assert!(atriai::is_immediate_stop(text, "Atri"), "{text}");
        }
        for text in ["стопка", "скажи слово стоп", "нажми стоп", "стоп музыка играет"] {
            assert!(!atriai::is_immediate_stop(text, "Atri"), "{text}");
        }
    }

    #[test]
    fn asr_wav_conditioning_pads_and_normalizes() {
        // 100 ms soft 1000-amplitude speech (1600 samples) -> must gain normalize & pad >= 1.2s (38444B WAV)
        let mut pcm = Vec::with_capacity(1600 * 2);
        for i in 0..1600usize {
            let s = if i % 2 == 0 { 1100i16 } else { -900i16 };
            pcm.extend_from_slice(&s.to_le_bytes());
        }
        let wav = atriai::prepare_asr_wav(&pcm);
        assert!(wav.starts_with(b"RIFF"));
        assert!(wav.len() >= 44 + 14400 * 2);
        // Leading 80ms (1280 samples) must be zero silence
        assert!(wav[44..44 + 1280 * 2].iter().all(|&b| b == 0));
        // Normalized peak should be lifted (> 3500)
        let peak = wav[44 + 1280 * 2..]
            .chunks_exact(2)
            .map(|b| i16::from_le_bytes([b[0], b[1]]).abs())
            .max()
            .unwrap_or(0);
        assert!(peak >= 3500, "expected normalized peak >= 3500, got {peak}");
    }

    #[test]
    fn voice_bt_volume_stop_and_barge_in() {
        use atriai::{is_barge_in_stop, local_command, split_trailing_stop, DeviceCommand};

        assert_eq!(
            local_command("Атри включи блютуз", "Atri"),
            Some(DeviceCommand::BluetoothOn)
        );
        assert_eq!(
            local_command("включи режим блютуз", ""),
            Some(DeviceCommand::BluetoothOn)
        );
        assert_eq!(
            local_command("выключи блютуз", ""),
            Some(DeviceCommand::BluetoothOff)
        );
        assert_eq!(
            local_command("Атри отключи bluetooth", "Atri"),
            Some(DeviceCommand::BluetoothOff)
        );

        assert_eq!(
            local_command("Атри громкость на 30", "Atri"),
            Some(DeviceCommand::VolumeSet(30))
        );
        assert_eq!(
            local_command("сделай тише на 15", ""),
            Some(DeviceCommand::VolumeDown(15))
        );
        assert_eq!(
            local_command("погромче", ""),
            Some(DeviceCommand::VolumeUp(10))
        );
        assert_eq!(
            local_command("громкость двадцать пять", ""),
            Some(DeviceCommand::VolumeSet(25))
        );

        assert_eq!(
            split_trailing_stop("Атри какая погода стоп", "Atri"),
            ("какая погода".to_string(), true)
        );
        assert_eq!(
            split_trailing_stop("Атри какая погода", "Atri"),
            ("какая погода".to_string(), false)
        );

        assert!(is_barge_in_stop("я работаю стоп", "Я работаю стабильно"));
        assert!(is_barge_in_stop("замолчи пожалуйста", "Привет"));
        assert!(is_barge_in_stop("завались", "Скорее всего"));
        assert!(is_barge_in_stop("ну все стоп", "Привет"));
        assert!(!is_barge_in_stop("я работаю стабильно", "Я работаю стабильно"));
    }

    #[test]
    fn fuzzy_atri_and_extended_cancel_phrases() {
        use atriai::{
            is_cancel_phrase, matches_wake_word, split_trailing_stop, strip_wake_word,
        };

        assert!(matches_wake_word("адре", "Atri"));
        assert!(matches_wake_word("эй адре какая погода", "Atri"));
        assert!(!matches_wake_word("адрес сделай громкость десять", "Atri"));
        assert!(!matches_wake_word("адрес", "Atri"));
        assert!(!matches_wake_word("армия идёт", "Atri"));
        assert!(!matches_wake_word("актриса играет", "Atri"));
        assert!(matches_wake_word("одри привет", "Atri"));
        assert!(!matches_wake_word("утром прохладно", "Atri"));
        assert!(!matches_wake_word("внутри пусто", "Atri"));
        assert_eq!(strip_wake_word("Эй адре громче", "Atri"), "громче");

        assert!(is_cancel_phrase("завались"));
        assert!(is_cancel_phrase("всё стоп"));
        assert!(is_cancel_phrase("ладно хватит уже"));
        assert!(is_cancel_phrase("не слушай"));
        assert!(!is_cancel_phrase("расскажи про слово стоп"));

        assert_eq!(
            split_trailing_stop("всё стоп", "Atri"),
            ("все стоп".to_string(), false)
        );
        assert_eq!(
            split_trailing_stop("какая погода хватит", "Atri"),
            ("какая погода".to_string(), true)
        );

        let c_wake = atriai::build_chime_pcm("wake", None);
        let c_up_lo = atriai::build_chime_pcm("volume_up", Some(15));
        let c_up_hi = atriai::build_chime_pcm("volume_up", Some(85));
        let c_dn = atriai::build_chime_pcm("volume_down", Some(10));
        let c_bt_on = atriai::build_chime_pcm("bt_on", None);
        let c_bt_off = atriai::build_chime_pcm("bt_off", None);
        let c_cancel = atriai::build_chime_pcm("cancel", None);
        assert!(c_wake.len() > 4000 && c_bt_on.len() > 8000 && c_cancel.len() > 2000);
        assert_ne!(c_up_lo, c_up_hi);
        assert_ne!(c_up_lo, c_dn);
        assert_ne!(c_bt_on, c_bt_off);
        assert_ne!(c_wake, c_cancel);
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
        assert_eq!(rotaryd::woofer_volume_to_raw(40, 255), rotaryd::volume_to_raw(40, 255));
        assert!(rotaryd::woofer_volume_to_raw(100, 255) <= rotaryd::volume_to_raw(40, 255) + 4);
        assert!(rotaryd::woofer_volume_to_raw(100, 255) < rotaryd::volume_to_raw(100, 255));
    }

    #[test]
    fn sha256_known_vector() {
        let digest = sys::hex_lower(&sys::sha256_bytes(b"abc"));
        assert_eq!(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }

    #[test]
    fn wifi_up_prevents_false_wifi_error_on_server_unreachable() {
        let msg = "Connect 192.168.1.107:8090: No route to host (os error 113)";
        assert_eq!(status::classify_error_kind(msg, "server", true), "server");
        assert_eq!(status::classify_error_kind(msg, "server", false), "network");
        assert_eq!(status::classify_error_kind(msg, "update", true), "update");
        assert_eq!(status::classify_error_kind("TLS handshake error", "server", true), "tls");
    }

    #[test]
    fn local_ai_timers_alarms_and_device_intents() {
        use local_ai::{
            parse_local_intent, parse_russian_alarm_time, parse_russian_duration_secs, LocalIntent,
        };

        assert_eq!(parse_russian_duration_secs(&["на", "5", "минут"]), Some(300));
        assert_eq!(parse_russian_duration_secs(&["тридцать", "секунд"]), Some(30));
        assert_eq!(parse_russian_duration_secs(&["полчаса"]), Some(1800));
        assert_eq!(parse_russian_duration_secs(&["один", "час", "двадцать", "минут"]), Some(4800));

        assert_eq!(parse_russian_alarm_time(&["на", "7", "утра"]), Some((7, 0)));
        assert_eq!(parse_russian_alarm_time(&["на", "семь", "вечера"]), Some((19, 0)));
        assert_eq!(parse_russian_alarm_time(&["в", "восемь", "тридцать"]), Some((8, 30)));
        assert_eq!(parse_russian_alarm_time(&["на", "полвосьмого"]), Some((7, 30)));
        assert_eq!(parse_russian_alarm_time(&["на", "23:45"]), Some((23, 45)));

        assert_eq!(
            parse_local_intent("Атри поставь таймер на 5 минут", "Atri"),
            Some(LocalIntent::SetTimer { secs: 300 })
        );
        assert_eq!(
            parse_local_intent("Атри отмени таймер", "Atri"),
            Some(LocalIntent::CancelTimers { target_secs: None, index: None, all: false })
        );
        assert_eq!(
            parse_local_intent("Атри поставь будильник на 7 30", "Atri"),
            Some(LocalIntent::SetAlarm { hour: 7, minute: 30 })
        );
        assert_eq!(
            parse_local_intent("удали будильник", ""),
            Some(LocalIntent::CancelAlarms)
        );
        assert_eq!(
            parse_local_intent("сколько времени", ""),
            Some(LocalIntent::QueryTime)
        );
        assert_eq!(
            parse_local_intent("включи подсветку радуга", ""),
            Some(LocalIntent::RingMode("rainbow".into()))
        );
        assert_eq!(
            parse_local_intent("выключи подсветку", ""),
            Some(LocalIntent::RingMode("off".into()))
        );
    }

    #[test]
    fn hardware_manifest_parses_custom_speaker_and_defaults() {
        let def = hardware::HardwareManifest::default();
        assert_eq!(def.board, "station-max");
        assert!(def.has_display());
        assert_eq!((def.display_width, def.display_height, def.ring_leds), (25, 16, 24));
        assert_eq!((def.amp_tweeter_addr, def.amp_woofer_addr), (0x2a, 0x2b));

        let mini = hardware::HardwareManifest::from_json_str(
            r#"{
              "schema": 1,
              "board": "station-mini",
              "name": "AtriStation Mini",
              "vendor": "yandex",
              "soc": "g12a",
              "dtb": "meson-g12a-station-mini.dtb",
              "display": {"type": "none", "width": 0, "height": 0},
              "led_ring": {"count": 12},
              "audio": {"amp_i2c_bus": 1, "amp_tweeter_addr": "0x2c", "mic_channels": 4},
              "features": {"rotary": false, "als": true, "zigbee": false, "bluetooth": true}
            }"#,
        )
        .unwrap();
        assert_eq!(mini.board, "station-mini");
        assert!(!mini.has_display());
        assert!(mini.has_ring());
        assert_eq!(mini.ring_leds, 12);
        assert_eq!((mini.amp_i2c_bus, mini.amp_tweeter_addr, mini.mic_channels), (1, 0x2c, 4));
        assert!(!mini.has_rotary && !mini.has_zigbee && mini.has_als && mini.has_bt);
    }

    #[test]
    fn supervise_backoff_and_health_json() {
        assert_eq!(supervise::next_backoff_secs(1, 30), 1);
        assert_eq!(supervise::next_backoff_secs(2, 30), 2);
        assert_eq!(supervise::next_backoff_secs(3, 30), 4);
        assert_eq!(supervise::next_backoff_secs(6, 30), 30);
        assert_eq!(supervise::next_backoff_secs(99, 30), 30);

        let entry = supervise::format_health_entry("running", 1234, 2, Some(1), 100, 105);
        assert_eq!(entry.get_str("state"), Some("running"));
        assert_eq!(entry.get("pid").and_then(|v| v.as_i64()), Some(1234));
        assert_eq!(entry.get("restarts").and_then(|v| v.as_i64()), Some(2));
    }

    #[test]
    fn timer_display_renders_digits_and_progress_bar() {
        // 50% remaining on a 5-minute timer -> digits visible + half-lit bottom bar on row 14
        let half = light::render_timer_frame(150, 300, true, false, 200, 2, None);
        let full = light::render_timer_frame(300, 300, true, false, 200, 2, None);
        assert_eq!(half.len(), full.len());
        let bright_half = (1..=23).filter(|&x| half[(14 * 25 + x) * 3] >= 180).count();
        let bright_full = (1..=23).filter(|&x| full[(14 * 25 + x) * 3] >= 180).count();
        assert_eq!(bright_full, 23);
        assert!((10..=13).contains(&bright_half), "expected ~12 bright bar pixels, got {bright_half}");
        // Clock with bottom timer bar on row 15
        let clk = light::render_clock_with_timer_bar("14:25", false, 200, 150, 300);
        let clk_bar = (1..=23).filter(|&x| clk[(15 * 25 + x) * 3] >= 180).count();
        assert!((10..=13).contains(&clk_bar));
    }

    #[test]
    fn multi_timer_show_pin_and_hide_intents() {
        use local_ai::{parse_local_intent, LocalIntent};
        assert_eq!(
            parse_local_intent("Атри, какие таймеры есть", "Atri"),
            Some(LocalIntent::QueryTimers)
        );
        assert_eq!(
            parse_local_intent("Атри, покажи таймер на 20 минут", "Atri"),
            Some(LocalIntent::ShowTimer { target_secs: Some(1200), index: None, pin: false })
        );
        assert_eq!(
            parse_local_intent("Атри, покажи второй таймер и оставь на экране", "Atri"),
            Some(LocalIntent::ShowTimer { target_secs: None, index: Some(1), pin: true })
        );
        assert_eq!(
            parse_local_intent("Атри, оставь на экране", "Atri"),
            Some(LocalIntent::ShowTimer { target_secs: None, index: None, pin: true })
        );
        assert_eq!(
            parse_local_intent("Атри, убери таймер с экрана", "Atri"),
            Some(LocalIntent::HideTimerDisplay)
        );
        assert_eq!(
            parse_local_intent("Атри, отмени таймер", "Atri"),
            Some(LocalIntent::CancelTimers { target_secs: None, index: None, all: false })
        );
        assert_eq!(
            parse_local_intent("Атри, удали таймер на 20 минут", "Atri"),
            Some(LocalIntent::CancelTimers { target_secs: Some(1200), index: None, all: false })
        );
        assert_eq!(
            parse_local_intent("Атри, удали второй таймер", "Atri"),
            Some(LocalIntent::CancelTimers { target_secs: None, index: Some(1), all: false })
        );
        assert_eq!(
            parse_local_intent("Атри, удали все таймеры", "Atri"),
            Some(LocalIntent::CancelTimers { target_secs: None, index: None, all: true })
        );
    }

    #[test]
    fn hardware_identity_deterministic_and_unique_across_macs() {
        let a1 = hardware::compute_hardware_identity("AA:BB:CC:11:22:33", "11:22:33:44:55:66", "", "sn01");
        let a2 = hardware::compute_hardware_identity("aa:bb:cc:11:22:33", "11:22:33:44:55:66", "", "SN01");
        let b1 = hardware::compute_hardware_identity("AA:BB:CC:11:22:34", "11:22:33:44:55:66", "", "sn01");
        assert_eq!(a1.device_id, a2.device_id);
        assert_eq!(a1.device_key, a2.device_key);
        assert_eq!(a1.hw_hash, a2.hw_hash);
        assert!(a1.device_id.starts_with("atri-"));
        assert_eq!(a1.device_key.len(), 64);
        assert_ne!(a1.device_id, b1.device_id);
        assert_ne!(a1.device_key, b1.device_key);
        // Music icon renders lit pixels
        let heart = light::render_music_icon_frame("like", 200);
        assert!(heart.iter().filter(|&&v| v >= 180).count() > 30);
    }
}
