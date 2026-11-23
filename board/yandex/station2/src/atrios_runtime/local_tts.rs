use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

const DEFAULT_VOICE_DIR: &str = "/usr/share/atrios/voice";
const SR: u32 = 16000;

fn voice_dir() -> PathBuf {
    if let Ok(d) = env::var("ATRI_VOICE_PACK_DIR") {
        if !d.trim().is_empty() {
            return PathBuf::from(d);
        }
    }
    PathBuf::from(DEFAULT_VOICE_DIR)
}

fn normalize_phrase(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    for ch in text.chars() {
        for lc in ch.to_lowercase() {
            let c = if lc == 'ё' { 'е' } else { lc };
            if c.is_alphanumeric() {
                out.push(c);
            } else {
                out.push(' ');
            }
        }
    }
    out.split_whitespace().collect::<Vec<_>>().join(" ")
}

#[derive(Clone, Copy)]
enum NumGender {
    Masc,
    FemNom, // одна, две
    FemAcc, // одну, две
}

fn push_number_clips(n: u64, gender: NumGender, out: &mut Vec<&'static str>) -> bool {
    if n > 59 {
        return false;
    }
    if n < 20 {
        let key = match (n, gender) {
            (0, _) => "n_0",
            (1, NumGender::Masc) => "n_1",
            (1, NumGender::FemNom) => "n_1f",
            (1, NumGender::FemAcc) => "n_1a",
            (2, NumGender::Masc) => "n_2",
            (2, NumGender::FemNom | NumGender::FemAcc) => "n_2f",
            (3, _) => "n_3",
            (4, _) => "n_4",
            (5, _) => "n_5",
            (6, _) => "n_6",
            (7, _) => "n_7",
            (8, _) => "n_8",
            (9, _) => "n_9",
            (10, _) => "n_10",
            (11, _) => "n_11",
            (12, _) => "n_12",
            (13, _) => "n_13",
            (14, _) => "n_14",
            (15, _) => "n_15",
            (16, _) => "n_16",
            (17, _) => "n_17",
            (18, _) => "n_18",
            (19, _) => "n_19",
            _ => return false,
        };
        out.push(key);
        return true;
    }
    let tens = match n / 10 {
        2 => "n_20",
        3 => "n_30",
        4 => "n_40",
        5 => "n_50",
        _ => return false,
    };
    out.push(tens);
    let rem = n % 10;
    if rem > 0 {
        push_number_clips(rem, gender, out);
    }
    true
}

fn unit_clip(word: &str) -> Option<(&'static str, NumGender)> {
    match word {
        "час" => Some(("u_chas", NumGender::Masc)),
        "часа" => Some(("u_chasa", NumGender::Masc)),
        "часов" => Some(("u_chasov", NumGender::Masc)),
        "минута" => Some(("u_minuta", NumGender::FemNom)),
        "минуту" => Some(("u_minutu", NumGender::FemAcc)),
        "минуты" => Some(("u_minuty", NumGender::FemNom)),
        "минут" => Some(("u_minut", NumGender::FemNom)),
        "секунда" => Some(("u_sekunda", NumGender::FemNom)),
        "секунду" => Some(("u_sekundu", NumGender::FemAcc)),
        "секунды" => Some(("u_sekundy", NumGender::FemNom)),
        "секунд" => Some(("u_sekund", NumGender::FemNom)),
        _ => None,
    }
}

fn parse_time_or_duration_tokens(tokens: &[&str], out: &mut Vec<&'static str>) -> bool {
    let mut i = 0usize;
    let mut matched_any = false;
    while i < tokens.len() {
        let tok = tokens[i];
        if tok == "ровно" {
            out.push("rovno");
            matched_any = true;
            i += 1;
            continue;
        }
        if tok == "установлен" || tok == "установлена" {
            i += 1;
            continue;
        }
        if let Ok(n) = tok.parse::<u64>() {
            if i + 1 < tokens.len() {
                if let Some((u_key, gender)) = unit_clip(tokens[i + 1]) {
                    if !push_number_clips(n, gender, out) {
                        return false;
                    }
                    out.push(u_key);
                    matched_any = true;
                    i += 2;
                    continue;
                }
            }
            return false;
        }
        return false;
    }
    matched_any
}

/// Plan a sequence of AtriVoice offline clip keys for a Russian response string.
pub fn plan_voice_clips(text: &str) -> Option<Vec<&'static str>> {
    let norm = normalize_phrase(text);
    if norm.is_empty() {
        return None;
    }
    // 1. Exact fixed phrases
    let fixed = match norm.as_str() {
        "хорошо все таймеры отменены" => Some("timer_cancel"),
        "активных таймеров сейчас нет" => Some("timer_none"),
        "время вышло таймер сработал" => Some("timer_ring"),
        "хорошо будильники отменены" => Some("alarm_cancel"),
        "будильников пока нет" => Some("alarm_none"),
        "доброе утро сработал будильник" | "доброе утро пора вставать будильник звонит" => Some("alarm_ring"),
        "блютуз включен" | "блютус включен" => Some("bt_on"),
        "блютуз выключен" | "блютус выключен" => Some("bt_off"),
        "извини сервер сейчас недоступен я работаю в офлайн режиме" => Some("offline_unhandled"),
        "готово" => Some("ok"),
        _ => None,
    };
    if let Some(k) = fixed {
        return Some(vec![k]);
    }

    // 2. Dynamic time / timer / alarm phrases
    let mut clips: Vec<&'static str> = Vec::new();
    let rest_str = if let Some(r) = norm.strip_prefix("сейчас ") {
        clips.push("lead_now");
        r
    } else if let Some(r) = norm.strip_prefix("поставила таймер на ") {
        clips.push("lead_timer_set");
        r
    } else if let Some(r) = norm.strip_prefix("до конца таймера осталось ") {
        clips.push("lead_timer_left");
        r
    } else if let Some(r) = norm.strip_prefix("будильник на ") {
        clips.push("lead_alarm_set");
        r
    } else if let Some(r) = norm.strip_prefix("будильник установлен на ") {
        clips.push("lead_alarm_set");
        r
    } else if let Some(r) = norm.strip_prefix("будильник стоит на ") {
        clips.push("lead_alarm_at");
        r
    } else {
        return None;
    };

    let toks: Vec<&str> = rest_str.split_whitespace().collect();
    if parse_time_or_duration_tokens(&toks, &mut clips) {
        return Some(clips);
    }

    // Graceful fallback for huge durations (>59 hours)
    if clips.first() == Some(&"lead_timer_set") {
        return Some(vec!["timer_set_generic"]);
    }
    if clips.first() == Some(&"lead_alarm_set") {
        return Some(vec!["alarm_set_generic"]);
    }
    None
}

fn read_wav_pcm16_mono(path: &Path) -> Option<(Vec<i16>, u32)> {
    let data = fs::read(path).ok()?;
    if data.len() < 44 || &data[0..4] != b"RIFF" || &data[8..12] != b"WAVE" {
        return None;
    }
    let mut off = 12usize;
    let mut sr = SR;
    let mut channels = 1u16;
    let mut bits = 16u16;
    while off + 8 <= data.len() {
        let id = &data[off..off + 4];
        let sz = u32::from_le_bytes([data[off + 4], data[off + 5], data[off + 6], data[off + 7]]) as usize;
        off += 8;
        if off + sz > data.len() {
            break;
        }
        if id == b"fmt " && sz >= 16 {
            channels = u16::from_le_bytes([data[off + 2], data[off + 3]]);
            sr = u32::from_le_bytes([data[off + 4], data[off + 5], data[off + 6], data[off + 7]]);
            bits = u16::from_le_bytes([data[off + 14], data[off + 15]]);
        } else if id == b"data" && channels == 1 && bits == 16 {
            let pcm: Vec<i16> = data[off..off + sz]
                .chunks_exact(2)
                .map(|b| i16::from_le_bytes([b[0], b[1]]))
                .collect();
            return Some((pcm, sr));
        }
        off += sz + (sz & 1);
    }
    None
}

fn encode_wav_pcm16_mono(samples: &[i16], sr: u32) -> Vec<u8> {
    let data_bytes = (samples.len() * 2) as u32;
    let mut out = Vec::with_capacity(44 + data_bytes as usize);
    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&(36 + data_bytes).to_le_bytes());
    out.extend_from_slice(b"WAVE");
    out.extend_from_slice(b"fmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes()); // PCM
    out.extend_from_slice(&1u16.to_le_bytes()); // mono
    out.extend_from_slice(&sr.to_le_bytes());
    out.extend_from_slice(&(sr * 2).to_le_bytes());
    out.extend_from_slice(&2u16.to_le_bytes());
    out.extend_from_slice(&16u16.to_le_bytes());
    out.extend_from_slice(b"data");
    out.extend_from_slice(&data_bytes.to_le_bytes());
    for &s in samples {
        out.extend_from_slice(&s.to_le_bytes());
    }
    out
}

fn try_external_offline_tts(text: &str) -> Option<Vec<u8>> {
    // 1. Optional Piper C++ binary if user installs a Russian .onnx model in /usr/share/atrios/piper/
    let piper_bin = Path::new("/usr/bin/piper");
    let piper_dir = Path::new("/usr/share/atrios/piper");
    if piper_bin.exists() && piper_dir.is_dir() {
        if let Ok(entries) = fs::read_dir(piper_dir) {
            if let Some(model) = entries
                .flatten()
                .map(|e| e.path())
                .find(|p| p.extension().and_then(|s| s.to_str()) == Some("onnx"))
            {
                let mut child = Command::new(piper_bin)
                    .args(["--model", &model.to_string_lossy(), "--output_file", "-"])
                    .stdin(Stdio::piped())
                    .stdout(Stdio::piped())
                    .stderr(Stdio::null())
                    .spawn()
                    .ok()?;
                if let Some(mut stdin) = child.stdin.take() {
                    use std::io::Write;
                    let _ = stdin.write_all(text.as_bytes());
                }
                if let Ok(out) = child.wait_with_output() {
                    if out.status.success() && out.stdout.len() > 44 && &out.stdout[0..4] == b"RIFF" {
                        return Some(out.stdout);
                    }
                }
            }
        }
    }
    // 2. Optional espeak-ng fallback if installed
    for bin in ["/usr/bin/espeak-ng", "/usr/bin/espeak"] {
        if Path::new(bin).exists() {
            if let Ok(out) = Command::new(bin)
                .args(["-v", "ru", "-s", "155", "--stdout", text])
                .stdout(Stdio::piped())
                .stderr(Stdio::null())
                .output()
            {
                if out.status.success() && out.stdout.len() > 44 && &out.stdout[0..4] == b"RIFF" {
                    return Some(out.stdout);
                }
            }
        }
    }
    None
}

/// Synthesize offline speech as a standard 16 kHz PCM16 mono WAV using the built-in
/// AtriVoice phrase bank (zero Python, zero CPU), falling back to Piper/espeak if present.
pub fn synthesize_offline_wav(text: &str) -> Option<Vec<u8>> {
    if let Some(keys) = plan_voice_clips(text) {
        let dir = voice_dir();
        let mut combined: Vec<i16> = Vec::new();
        let mut sr_out = SR;
        let gap_samples = (SR as usize) * 28 / 1000; // 28 ms natural word gap
        let mut all_found = true;
        for (idx, k) in keys.iter().enumerate() {
            let p = dir.join(format!("{k}.wav"));
            if let Some((pcm, sr)) = read_wav_pcm16_mono(&p) {
                sr_out = sr;
                if idx > 0 {
                    combined.extend(std::iter::repeat(0i16).take(gap_samples));
                }
                combined.extend_from_slice(&pcm);
            } else {
                all_found = false;
                break;
            }
        }
        if all_found && !combined.is_empty() {
            return Some(encode_wav_pcm16_mono(&combined, sr_out));
        }
    }
    try_external_offline_tts(text)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn offline_voice_clip_planning_covers_time_timers_alarms() {
        assert_eq!(
            plan_voice_clips("Сейчас 14 часов 25 минут."),
            Some(vec!["lead_now", "n_14", "u_chasov", "n_20", "n_5", "u_minut"])
        );
        assert_eq!(
            plan_voice_clips("Сейчас 9 часов ровно."),
            Some(vec!["lead_now", "n_9", "u_chasov", "rovno"])
        );
        assert_eq!(
            plan_voice_clips("Поставила таймер на 1 минуту 2 секунды!"),
            Some(vec!["lead_timer_set", "n_1a", "u_minutu", "n_2f", "u_sekundy"])
        );
        assert_eq!(
            plan_voice_clips("Будильник на 7 часов 30 минут установлен!"),
            Some(vec!["lead_alarm_set", "n_7", "u_chasov", "n_30", "u_minut"])
        );
        assert_eq!(
            plan_voice_clips("Хорошо, все таймеры отменены."),
            Some(vec!["timer_cancel"])
        );
        assert_eq!(
            plan_voice_clips("Блютуз включён."),
            Some(vec!["bt_on"])
        );
    }
}
