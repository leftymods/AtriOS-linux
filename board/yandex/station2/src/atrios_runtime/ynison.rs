use std::fs;
use std::os::unix::net::UnixDatagram;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use crate::https::{WsFrame, WssClient};
use crate::json::{obj, Json};
use crate::ymusic::{load_auth, read_status, YMusicStatus, YMUSIC_CMD_SOCK};

const YNISON_REDIRECT_URL: &str =
    "wss://ynison.music.yandex.ru/redirector.YnisonRedirectService/GetRedirectToYnison";
const YNISON_STATE_PATH: &str = "/ynison_state.YnisonStateService/PutYnisonState";
const YNISON_ORIGIN: &str = "https://music.yandex.ru";
const YNISON_APP_NAME: &str = "AtriStation";
const YNISON_APP_VERSION: &str = "1.13.0";
const YNISON_DEVICE_ID_FILE: &str = "/var/lib/atrios/ynison_device_id";

static RID_SEQ: AtomicU64 = AtomicU64::new(1);

fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

fn next_version_str() -> String {
    let n = RID_SEQ.fetch_add(1, Ordering::SeqCst);
    let ts = (now_ms() as u64).wrapping_mul(1000).wrapping_add(n & 0x3FF);
    ts.to_string()
}

fn next_rid() -> String {
    let n = RID_SEQ.fetch_add(1, Ordering::SeqCst);
    let t = now_ms() as u64;
    format!(
        "{:08x}-{:04x}-4{:03x}-8{:03x}-{:012x}",
        (t >> 16) as u32,
        (t & 0xFFFF) as u16,
        (n & 0x0FFF) as u16,
        ((t ^ n) & 0x0FFF) as u16,
        t.wrapping_mul(6364136223846793005).wrapping_add(n) & 0xFFFF_FFFF_FFFF
    )
}

fn ynison_device_id() -> String {
    if let Ok(s) = fs::read_to_string(YNISON_DEVICE_ID_FILE) {
        let trimmed = s.trim();
        if trimmed.len() >= 12 {
            return trimmed.to_string();
        }
    }
    let hw = crate::hardware::device_identity();
    let base = format!("atrios-ynison-{}", hw.device_id);
    let mut id = crate::ymusic::md5_hex(base.as_bytes());
    id.truncate(16);
    let _ = fs::create_dir_all("/var/lib/atrios");
    let _ = fs::write(YNISON_DEVICE_ID_FILE, format!("{id}\n"));
    id
}

fn build_ws_protocol(device_id: &str, ticket: Option<&str>, session_id: Option<&str>) -> String {
    let dev_info = obj(vec![
        ("app_name", Json::Str(YNISON_APP_NAME.into())),
        ("app_version", Json::Str(YNISON_APP_VERSION.into())),
        ("type", Json::Num(1.0)),
    ])
    .to_string_compact();
    let mut fields = vec![
        ("Ynison-Device-Id", Json::Str(device_id.into())),
        ("Ynison-Device-Info", Json::Str(dev_info)),
    ];
    if let Some(t) = ticket {
        fields.push(("Ynison-Redirect-Ticket", Json::Str(t.into())));
    }
    if let Some(s) = session_id {
        fields.push(("Ynison-Session-Id", Json::Str(s.into())));
    }
    format!("Bearer, v2, {}", obj(fields).to_string_compact())
}

fn make_version_block(device_id: &str) -> Json {
    obj(vec![
        ("device_id", Json::Str(device_id.into())),
        ("version", Json::Str(next_version_str())),
        ("timestamp_ms", Json::Str(now_ms().to_string())),
    ])
}

fn build_device_block(device_id: &str, vol_0_1: f64) -> Json {
    obj(vec![
        (
            "info",
            obj(vec![
                ("device_id", Json::Str(device_id.into())),
                ("title", Json::Str("AtriStation".into())),
                ("type", Json::Str("WEB".into())),
                ("app_name", Json::Str(YNISON_APP_NAME.into())),
                ("app_version", Json::Str(YNISON_APP_VERSION.into())),
            ]),
        ),
        (
            "capabilities",
            obj(vec![
                ("can_be_player", Json::Bool(true)),
                ("can_be_remote_controller", Json::Bool(false)),
                ("volume_granularity", Json::Num(100.0)),
            ]),
        ),
        (
            "volume_info",
            obj(vec![("volume", Json::Num(vol_0_1.clamp(0.0, 1.0)))]),
        ),
    ])
}

fn build_initial_full_state(device_id: &str, vol_0_1: f64) -> String {
    let player_state = obj(vec![
        (
            "status",
            obj(vec![
                ("paused", Json::Bool(true)),
                ("duration_ms", Json::Str("0".into())),
                ("progress_ms", Json::Str("0".into())),
                ("playback_speed", Json::Num(1.0)),
                ("version", make_version_block(device_id)),
            ]),
        ),
        (
            "player_queue",
            obj(vec![
                ("current_playable_index", Json::Num(-1.0)),
                ("entity_id", Json::Str(String::new())),
                ("entity_type", Json::Str("VARIOUS".into())),
                ("playable_list", Json::Arr(Vec::new())),
                ("options", obj(vec![("repeat_mode", Json::Str("NONE".into()))])),
                ("entity_context", Json::Str("BASED_ON_ENTITY_BY_DEFAULT".into())),
                ("version", make_version_block(device_id)),
                ("from_optional", Json::Str(String::new())),
            ]),
        ),
    ]);
    obj(vec![
        (
            "update_full_state",
            obj(vec![
                ("player_state", player_state),
                ("device", build_device_block(device_id, vol_0_1)),
                ("is_currently_active", Json::Bool(false)),
            ]),
        ),
        ("rid", Json::Str(next_rid())),
        ("player_action_timestamp_ms", Json::Str(now_ms().to_string())),
        (
            "activity_interception_type",
            Json::Str("DO_NOT_INTERCEPT_BY_DEFAULT".into()),
        ),
    ])
    .to_string_compact()
}

fn build_playing_status_msg(
    device_id: &str,
    paused: bool,
    progress_ms: i64,
    duration_ms: i64,
) -> String {
    obj(vec![
        (
            "update_playing_status",
            obj(vec![(
                "playing_status",
                obj(vec![
                    ("paused", Json::Bool(paused)),
                    ("duration_ms", Json::Str(duration_ms.max(0).to_string())),
                    ("progress_ms", Json::Str(progress_ms.max(0).to_string())),
                    ("playback_speed", Json::Num(1.0)),
                    ("version", make_version_block(device_id)),
                ]),
            )]),
        ),
        ("rid", Json::Str(next_rid())),
        ("player_action_timestamp_ms", Json::Str(now_ms().to_string())),
        (
            "activity_interception_type",
            Json::Str("DO_NOT_INTERCEPT_BY_DEFAULT".into()),
        ),
    ])
    .to_string_compact()
}

fn build_volume_info_msg(device_id: &str, vol_0_1: f64) -> String {
    obj(vec![
        (
            "update_volume_info",
            obj(vec![
                ("device_id", Json::Str(device_id.into())),
                (
                    "volume_info",
                    obj(vec![
                        ("volume", Json::Num(vol_0_1.clamp(0.0, 1.0))),
                        ("version", make_version_block(device_id)),
                    ]),
                ),
            ]),
        ),
        ("rid", Json::Str(next_rid())),
        ("player_action_timestamp_ms", Json::Str(now_ms().to_string())),
        (
            "activity_interception_type",
            Json::Str("DO_NOT_INTERCEPT_BY_DEFAULT".into()),
        ),
    ])
    .to_string_compact()
}

fn build_hybrid_full_state(device_id: &str, vol_0_1: f64, st: &YMusicStatus, claim_active: bool) -> String {
    let paused = st.state != "playing" && st.state != "loading";
    let pos = if !paused && st.started_ts > 0 {
        st.position_ms + (now_ms() - st.started_ts).max(0)
    } else {
        st.position_ms
    };
    let ids: Vec<String> = if !st.queue_ids.is_empty() {
        st.queue_ids.clone()
    } else if !st.track_id.is_empty() {
        vec![st.track_id.clone()]
    } else {
        Vec::new()
    };
    let cur_idx: i64 = if ids.is_empty() {
        -1
    } else {
        st.queue_index.min(ids.len().saturating_sub(1)) as i64
    };
    let playables: Vec<Json> = ids
        .iter()
        .enumerate()
        .map(|(i, tid)| {
            let mut f = vec![
                ("playable_id", Json::Str(tid.clone())),
                ("playable_type", Json::Str("TRACK".into())),
                ("from", Json::Str("AtriOS-speaker".into())),
            ];
            if i as i64 == cur_idx {
                if !st.title.is_empty() {
                    f.push(("title", Json::Str(st.title.clone())));
                }
                if !st.cover_url.is_empty() {
                    f.push(("cover_url_optional", Json::Str(st.cover_url.clone())));
                }
            }
            obj(f)
        })
        .collect::<Vec<Json>>();
    let entity_type = match st.mode.as_str() {
        "wave" | "station" => "RADIO",
        "playlist" | "likes" => "PLAYLIST",
        "album" => "ALBUM",
        _ => "VARIOUS",
    };
    let repeat_mode = if st.repeat { "ONE" } else { "NONE" };
    let player_state = obj(vec![
        (
            "status",
            obj(vec![
                ("paused", Json::Bool(paused)),
                ("duration_ms", Json::Str(st.duration_ms.max(0).to_string())),
                ("progress_ms", Json::Str(pos.max(0).to_string())),
                ("playback_speed", Json::Num(1.0)),
                ("version", make_version_block(device_id)),
            ]),
        ),
        (
            "player_queue",
            obj(vec![
                ("current_playable_index", Json::Num(cur_idx as f64)),
                ("entity_id", Json::Str(String::new())),
                ("entity_type", Json::Str(entity_type.into())),
                ("playable_list", Json::Arr(playables)),
                ("options", obj(vec![("repeat_mode", Json::Str(repeat_mode.into()))])),
                ("entity_context", Json::Str("BASED_ON_ENTITY_BY_DEFAULT".into())),
                ("version", make_version_block(device_id)),
                ("from_optional", Json::Str("AtriOS-speaker".into())),
            ]),
        ),
    ]);
    let interception = if claim_active {
        "INTERCEPT_IF_NO_ONE_ACTIVE"
    } else {
        "DO_NOT_INTERCEPT_BY_DEFAULT"
    };
    obj(vec![
        (
            "update_full_state",
            obj(vec![
                ("player_state", player_state),
                ("device", build_device_block(device_id, vol_0_1)),
                ("is_currently_active", Json::Bool(claim_active)),
            ]),
        ),
        ("rid", Json::Str(next_rid())),
        ("player_action_timestamp_ms", Json::Str(now_ms().to_string())),
        ("activity_interception_type", Json::Str(interception.into())),
    ])
    .to_string_compact()
}

fn send_ymusic_cmd(cmd: &str) {
    if let Ok(sock) = UnixDatagram::unbound() {
        let _ = sock.send_to(cmd.as_bytes(), YMUSIC_CMD_SOCK);
    }
}

fn get_redirect_ticket(token: &str, device_id: &str) -> Result<(String, String, String, u64), String> {
    let auth_hdr = format!("OAuth {token}");
    let proto = build_ws_protocol(device_id, None, None);
    let headers = [
        ("Authorization", auth_hdr.as_str()),
        ("Origin", YNISON_ORIGIN),
        ("Sec-WebSocket-Protocol", proto.as_str()),
    ];
    let mut ws = WssClient::connect(YNISON_REDIRECT_URL, &headers, 15)?;
    let frame = ws
        .recv_frame(15_000)?
        .ok_or("Timeout waiting for Ynison redirect response")?;
    let txt = match frame {
        WsFrame::Text(t) => t,
        WsFrame::Binary(b) => String::from_utf8_lossy(&b).into_owned(),
        WsFrame::Close(c) => return Err(format!("Redirector closed WS ({c})")),
    };
    let j = Json::parse(&txt)?;
    let host = j.get_str("host").unwrap_or("").trim().to_string();
    let ticket = j.get_str("redirect_ticket").unwrap_or("").trim().to_string();
    let session_id = j
        .get("session_id")
        .map(|v| match v {
            Json::Str(s) => s.clone(),
            Json::Num(n) => (*n as i64).to_string(),
            _ => String::new(),
        })
        .unwrap_or_default();
    let keepalive = j
        .get("keep_alive_params")
        .and_then(|k| k.get("keep_alive_time_seconds"))
        .and_then(|v| v.as_i64())
        .unwrap_or(25)
        .clamp(10, 45) as u64;
    if host.is_empty() || ticket.is_empty() {
        return Err("Ynison redirect missing host or ticket".into());
    }
    Ok((host, ticket, session_id, keepalive))
}

fn run_ynison_session(token: &str, device_id: &str, stop_flag: &'static AtomicBool) -> Result<(), String> {
    let (host, ticket, session_id, keepalive_sec) = get_redirect_ticket(token, device_id)?;
    let url = format!("wss://{host}{YNISON_STATE_PATH}");
    let auth_hdr = format!("OAuth {token}");
    let proto = build_ws_protocol(device_id, Some(&ticket), Some(&session_id));
    let headers = [
        ("Authorization", auth_hdr.as_str()),
        ("Origin", YNISON_ORIGIN),
        ("Sec-WebSocket-Protocol", proto.as_str()),
    ];
    let mut ws = WssClient::connect(&url, &headers, 15)?;
    let init_vol = (crate::bt_control::get_current_volume() as f64 / 100.0).clamp(0.0, 1.0);
    ws.send_text(&build_initial_full_state(device_id, init_vol))?;
    println!("[ynison] Connected as AtriStation (device_id={device_id}, host={host})");

    let mut is_active = false;
    let mut last_queue_sig = String::new();
    let mut last_idx: i64 = -1;
    let mut last_paused: Option<bool> = None;
    let mut last_remote_vol_pct: i32 = crate::bt_control::get_current_volume();
    let mut last_ping = Instant::now();
    let mut last_status_push = Instant::now();
    let mut last_pushed_track = String::new();
    let mut last_pushed_paused = true;

    while !stop_flag.load(Ordering::SeqCst) {
        // Auto-remove immediately when user logs out or switches Yandex token:
        // send clean RFC 6455 Close(1000) so Ynison removes AtriStation from official app right away!
        match load_auth() {
            Some(a) if a.token == token => {}
            _ => {
                println!("[ynison] Yandex Music logged out — sending clean Close(1000) to remove AtriStation");
                send_ymusic_cmd("stop");
                ws.send_close(1000);
                return Ok(());
            }
        }

        if last_ping.elapsed() >= Duration::from_secs(keepalive_sec) {
            ws.send_ping()?;
            last_ping = Instant::now();
        }

        // Hybrid Initiator + Status Sync:
        // 1) If speaker started playing autonomously (voice / button / wave), claim active device & publish full queue!
        // 2) If speaker is already active, push track/pause/progress/volume changes back to Ynison!
        if last_status_push.elapsed() >= Duration::from_secs(2) {
            let st = read_status();
            let cur_vol_pct = crate::bt_control::get_current_volume();
            if (cur_vol_pct - last_remote_vol_pct).abs() >= 1 {
                last_remote_vol_pct = cur_vol_pct;
                let _ = ws.send_text(&build_volume_info_msg(
                    device_id,
                    cur_vol_pct as f64 / 100.0,
                ));
            }
            let paused = st.state != "playing" && st.state != "loading";
            let local_queue_sig = st.queue_ids.join(",");
            if !paused && !st.track_id.is_empty() && st.mode != "ynison" {
                let queue_changed = local_queue_sig != last_queue_sig
                    || st.track_id != last_pushed_track
                    || !is_active;
                if queue_changed {
                    let _ = ws.send_text(&build_hybrid_full_state(
                        device_id,
                        cur_vol_pct as f64 / 100.0,
                        &st,
                        true,
                    ));
                    is_active = true;
                    last_queue_sig = local_queue_sig;
                    last_idx = st.queue_index as i64;
                    last_paused = Some(false);
                    last_pushed_track = st.track_id;
                    last_pushed_paused = false;
                } else {
                    let pos = if st.started_ts > 0 {
                        st.position_ms + (now_ms() - st.started_ts).max(0)
                    } else {
                        st.position_ms
                    };
                    let _ = ws.send_text(&build_playing_status_msg(
                        device_id,
                        false,
                        pos,
                        st.duration_ms,
                    ));
                }
            } else if is_active {
                let pos = if !paused && st.started_ts > 0 {
                    st.position_ms + (now_ms() - st.started_ts).max(0)
                } else {
                    st.position_ms
                };
                if st.track_id != last_pushed_track || paused != last_pushed_paused || !paused {
                    let _ = ws.send_text(&build_playing_status_msg(
                        device_id,
                        paused,
                        pos,
                        st.duration_ms,
                    ));
                    last_pushed_track = st.track_id;
                    last_pushed_paused = paused;
                }
            }
            last_status_push = Instant::now();
        }

        let Some(frame) = ws.recv_frame(300)? else {
            continue;
        };
        let txt = match frame {
            WsFrame::Text(t) => t,
            WsFrame::Binary(b) => String::from_utf8_lossy(&b).into_owned(),
            WsFrame::Close(c) => return Err(format!("Ynison WS closed ({c})")),
        };
        let Ok(msg) = Json::parse(&txt) else {
            continue;
        };

        // Handle server rebalance error codes
        if let Some(code) = msg
            .get("error")
            .and_then(|e| e.get_str("ynison_error_code"))
        {
            if code == "300100001" || code == "300100002" {
                return Err(format!("Ynison rebalance ({code})"));
            }
        }

        let active_id = msg.get_str("active_device_id_optional").unwrap_or("");
        let now_active = active_id == device_id;

        // Check volume update targeting our device
        if let Some(devs) = msg.get("devices").and_then(|v| v.as_arr()) {
            for d in devs {
                let did = d
                    .get("info")
                    .and_then(|i| i.get_str("device_id"))
                    .unwrap_or("");
                if did == device_id {
                    if let Some(v) = d
                        .get("volume_info")
                        .and_then(|vi| vi.get("volume"))
                        .and_then(|n| n.as_f64())
                    {
                        let pct = (v.clamp(0.0, 1.0) * 100.0).round() as i32;
                        if now_active && (pct - last_remote_vol_pct).abs() >= 1 {
                            last_remote_vol_pct = pct;
                            crate::bt_control::set_system_volume(pct);
                        }
                    }
                }
            }
        }

        if is_active && !now_active {
            println!("[ynison] Playback transferred away (active={active_id}), pausing speaker");
            send_ymusic_cmd("pause");
            is_active = false;
            last_paused = Some(true);
            continue;
        }
        is_active = now_active;
        if !now_active {
            continue;
        }

        let Some(ps) = msg.get("player_state") else {
            continue;
        };
        let status = ps.get("status");
        let queue = ps.get("player_queue");
        let status_author = status
            .and_then(|s| s.get("version"))
            .and_then(|v| v.get_str("device_id"))
            .unwrap_or("");
        let queue_author = queue
            .and_then(|q| q.get("version"))
            .and_then(|v| v.get_str("device_id"))
            .unwrap_or("");
        if status_author == device_id && queue_author == device_id {
            continue; // Own echo
        }

        let paused = status
            .and_then(|s| s.get("paused"))
            .and_then(|b| b.as_bool())
            .unwrap_or(false);

        if let Some(q) = queue {
            let cur_idx = q
                .get("current_playable_index")
                .and_then(|v| v.as_i64())
                .unwrap_or(-1);
            let mut ids: Vec<String> = Vec::new();
            if let Some(arr) = q.get("playable_list").and_then(|v| v.as_arr()) {
                for item in arr {
                    if let Some(pid) = item.get_str("playable_id") {
                        let pid = pid.trim();
                        if !pid.is_empty() {
                            ids.push(pid.to_string());
                        }
                    }
                }
            }
            if !ids.is_empty() && cur_idx >= 0 && (cur_idx as usize) < ids.len() {
                let win_start = (cur_idx as usize).saturating_sub(5);
                let win_end = (win_start + 40).min(ids.len());
                let rel_idx = (cur_idx as usize) - win_start;
                let joined = ids[win_start..win_end].join(",");
                if joined != last_queue_sig || cur_idx != last_idx {
                    last_queue_sig = joined.clone();
                    last_idx = cur_idx;
                    last_paused = Some(paused);
                    let mode_tag = if paused { "pause" } else { "play" };
                    let cmd = format!("ynison_sync:{mode_tag}:{rel_idx}:{joined}");
                    println!("[ynison] Syncing queue ({} tracks, idx={rel_idx}, {mode_tag})", win_end - win_start);
                    send_ymusic_cmd(&cmd);
                    continue;
                }
            }
        }

        if last_paused != Some(paused) {
            last_paused = Some(paused);
            send_ymusic_cmd(if paused { "pause" } else { "play" });
        }
    }
    Ok(())
}

pub fn spawn_ynison_thread(stop_flag: &'static AtomicBool) {
    thread::spawn(move || {
        let device_id = ynison_device_id();
        let mut backoff_sec = 2u64;
        while !stop_flag.load(Ordering::SeqCst) {
            let Some(auth) = load_auth() else {
                thread::sleep(Duration::from_secs(1));
                continue;
            };
            match run_ynison_session(&auth.token, &device_id, stop_flag) {
                Ok(()) => {
                    backoff_sec = 2;
                    thread::sleep(Duration::from_secs(2));
                }
                Err(e) => {
                    eprintln!("[ynison] Disconnected ({e}); retrying in {backoff_sec}s");
                    let mut slept = 0u64;
                    while slept < backoff_sec && !stop_flag.load(Ordering::SeqCst) {
                        thread::sleep(Duration::from_secs(1));
                        slept += 1;
                    }
                    backoff_sec = (backoff_sec * 2).min(30);
                }
            }
        }
    });
}
