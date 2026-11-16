"""Runtime behavior regressions with fake HTTPS, audio, sockets and hardware calls."""
import importlib.machinery
import importlib.util
import io
import json
import signal
import socket
import sys
import types
import urllib.request
import wave
from pathlib import Path

import pytest

RUNTIME = (Path(__file__).resolve().parents[1] / "board/yandex/station2/rootfs_overlay/usr/lib/atrios")
sys.path.insert(0, str(RUNTIME))
import atrios_https as https
import atrios_status as status


def load_script(name, monkeypatch):
    monkeypatch.setattr(signal, "signal", lambda *args: None)
    loader = importlib.machinery.SourceFileLoader("regression_" + name.replace("-", "_"), str(RUNTIME / name))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def wav_bytes():
    out = io.BytesIO()
    with wave.open(out, "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(16000)
        writer.writeframes(b"\0\0" * 160)
    return out.getvalue()


@pytest.fixture
def voice(monkeypatch):
    ai = load_script("atriai", monkeypatch)
    ai.events = []
    monkeypatch.setattr(ai, "load_config", lambda: {"token": "secret", "ca_file": "/trust.pem"})
    monkeypatch.setattr(ai, "_voice_call", lambda fn: fn())
    monkeypatch.setattr(ai, "set_vad_visual", lambda phase, **kw: ai.events.append(("visual", phase)))
    monkeypatch.setattr(ai, "set_error", lambda *args: ai.events.append(("error", args)))
    monkeypatch.setattr(ai, "write_status", lambda *args, **kw: None)
    monkeypatch.setattr(ai, "play_audio_bytes", lambda data: ai.events.append(("audio", data)))
    monkeypatch.setattr(ai, "execute_device_command", lambda action: ai.events.append(("local", action)))
    return ai


def fake_voice_services(ai, monkeypatch, *, text="Atri explain stars", audio=None):
    calls = []
    audio = wav_bytes() if audio is None else audio
    def get_bytes(url, **kwargs):
        calls.append((url, kwargs))
        if url.endswith("/asr"):
            with wave.open(io.BytesIO(kwargs["data"]), "rb") as reader:
                assert (reader.getnchannels(), reader.getsampwidth(), reader.getframerate()) == (1, 2, 16000)
            return json.dumps({"text": text}).encode()
        assert url.endswith("/tts")
        assert json.loads(kwargs["data"])["text"] == "Stars are hot"
        return audio
    def get_json(url, **kwargs):
        calls.append((url, kwargs))
        assert url.endswith("/chat") and kwargs["payload"] == {"text": text}
        return {"text": "Stars are hot", "emotion": "neutral"}
    monkeypatch.setattr(ai, "request_bytes", get_bytes)
    monkeypatch.setattr(ai, "request_json", get_json)
    return calls


def test_voice_https_asr_chat_tts_chain_preserves_auth_and_trust(voice, monkeypatch):
    calls = fake_voice_services(voice, monkeypatch)
    assert voice.send_phrase_to_server("https://assistant.example", b"\0\0" * 160, "Atri") == ("Atri explain stars", "Stars are hot")
    assert [url.rsplit("/", 1)[-1] for url, _ in calls] == ["asr", "chat", "tts"]
    assert all(kwargs["headers"]["Authorization"] == "Bearer secret" and kwargs["ca_file"] == "/trust.pem" for _, kwargs in calls)
    assert any(event[0] == "audio" for event in voice.events)
    assert voice.events[-1] == ("visual", "idle")


@pytest.mark.parametrize("pcm", [b"x", b"\0" * (16000 * 2 * 30 + 2)])
def test_voice_rejects_invalid_pcm_before_network(voice, monkeypatch, pcm):
    monkeypatch.setattr(voice, "request_bytes", lambda *a, **k: pytest.fail("network called"))
    with pytest.raises(ValueError):
        voice.send_phrase_to_server("https://assistant.example", pcm)


@pytest.mark.parametrize("audio", [b"RIFF", b"RIFF" + b"\0" * 40, wav_bytes()[:-20]])
def test_voice_rejects_malformed_or_truncated_wav(voice, monkeypatch, audio):
    fake_voice_services(voice, monkeypatch, audio=audio)
    with pytest.raises((ValueError, EOFError, wave.Error)):
        voice.send_phrase_to_server("https://assistant.example", b"\0\0" * 160, "Atri")
    assert not any(event[0] == "audio" for event in voice.events)
    assert voice.events[-1] == ("visual", "idle")


def test_voice_local_russian_command_bypasses_chat_and_tts(voice, monkeypatch):
    calls = fake_voice_services(voice, monkeypatch, text="\u0410\u0442\u0440\u0438, \u0433\u0440\u043e\u043c\u0447\u0435")
    voice.send_phrase_to_server("https://assistant.example", b"\0\0" * 160, "Atri")
    assert len(calls) == 1
    assert ("local", "volume_up") in voice.events


@pytest.mark.parametrize("text,wake,expected", [
    ("Atri, hello", "Atri", True),
    ("atrium", "Atri", False),
    ("patriarch", "Atri", False),
    ("\u0410\u0442\u0440\u0438, \u043f\u0440\u0438\u0432\u0435\u0442", "Atri", True),
    ("hello", "off", True),
])
def test_wake_word_boundaries(voice, text, wake, expected):
    assert voice.matches_wake_word(text, wake) is expected


@pytest.mark.parametrize("text,expected", [
    ("\u0410\u0442\u0440\u0438 \u0433\u0440\u043e\u043c\u0447\u0435", "volume_up"),
    ("\u0410\u0442\u0440\u0438 \u0442\u0438\u0448\u0435", "volume_down"),
    ("Atri next track", "next_track"),
    ("Atri what does next track mean", None),
    ("Atri volume upward", None),
])
def test_local_command_exact_phrase_boundaries(voice, text, expected):
    assert voice.local_command(text, "Atri") == expected


class Response:
    def __init__(self, chunks, length=None):
        self.chunks = iter(chunks)
        self.headers = {} if length is None else {"Content-Length": str(length)}
        self.read1_calls = 0
    def read(self, amount):
        pytest.fail("unbounded buffering read used instead of read1")
    def read1(self, amount):
        self.read1_calls += 1
        return next(self.chunks, b"")


def test_https_rejects_truncated_content_length():
    with pytest.raises(ValueError, match="[Tt]runcated"):
        https.read_limited(Response([b"RIFF"], 100), 1024)


def test_https_reads_exact_limit_and_rejects_overflow():
    assert https.read_limited(Response([b"1234"], 4), 4) == b"1234"
    with pytest.raises(ValueError):
        https.read_limited(Response([b"1234", b"5"]), 4)
    response = Response([], 100)
    with pytest.raises(ValueError):
        https.read_limited(response, 4)
    assert response.read1_calls == 0


def test_https_deadline_and_cancel_checked_between_stream_chunks(monkeypatch):
    tick = iter([0.0, 2.0])
    monkeypatch.setattr(https.time, "monotonic", lambda: next(tick))
    response = Response([b"1", b"2"])
    with pytest.raises(TimeoutError):
        https.read_limited(response, 20, deadline=1.0)
    assert response.read1_calls == 1
    with pytest.raises(InterruptedError):
        https.read_limited(Response([b"1"]), 20, cancel=lambda: True)


def test_https_redirect_strips_cross_host_token_and_rejects_downgrade():
    handler = https.SecureRedirect()
    req = urllib.request.Request("https://one.example/", headers={"Authorization": "Bearer secret"})
    redirect = handler.redirect_request(req, None, 302, "Found", {}, "https://two.example/")
    assert not redirect.has_header("Authorization")
    same = handler.redirect_request(req, None, 302, "Found", {}, "https://one.example/new")
    assert same.get_header("Authorization") == "Bearer secret"
    rejected_response = io.BytesIO()
    with pytest.raises(ValueError):
        handler.redirect_request(req, rejected_response, 302, "Found", {}, "http://two.example/")
    assert rejected_response.closed


@pytest.mark.parametrize("corrupt", [[], {"x": {"code": "bad", "expires": 1e20}},
    {"x": {"code": 3, "time": "bad", "expires": 1e20}, "y": {"code": 3, "time": 1, "expires": 1e20}}])
def test_error_state_rejects_corrupt_entries_without_crashing(tmp_path, monkeypatch, corrupt):
    monkeypatch.setattr(status, "ERROR_FILE", tmp_path / "errors.json")
    status.ERROR_FILE.write_text(json.dumps(corrupt))
    error = status.active_error()
    assert error is None or isinstance(error.get("time", 0), (int, float))


def test_error_state_expiry_and_clearing_are_per_subsystem(tmp_path, monkeypatch):
    monkeypatch.setattr(status, "ERROR_FILE", tmp_path / "errors.json")
    monkeypatch.setattr(status.time, "time", lambda: 100)
    status.set_error("voice", "network", "offline", ttl=10)
    status.set_error("ota", "signature", "bad signature", ttl=20)
    assert status.active_error()["code"] == 6
    status.set_error("voice")
    assert set(status.get_errors()) == {"ota"}
    monkeypatch.setattr(status.time, "time", lambda: 121)
    assert status.active_error() is None


def test_screen_renders_error_as_valid_nonempty_frame(monkeypatch):
    screen = load_script("screend", monkeypatch)
    frames = []
    monkeypatch.setattr(screen, "send_frame", frames.append)
    screen.render_error(6)
    assert len(frames) == 1 and len(frames[0]) == 25 * 16 * 3
    assert any(frames[0])


@pytest.fixture
def bluetooth(monkeypatch):
    bt = load_script("bt-control", monkeypatch)
    monkeypatch.setattr(bt.subprocess, "run", lambda *a, **k: types.SimpleNamespace(returncode=0))
    monkeypatch.setattr(bt.subprocess, "Popen", lambda *a, **k: None)
    monkeypatch.setattr(bt, "build_status_payload", lambda: {"type": "status"})
    monkeypatch.setattr(bt, "get_current_volume", lambda: 30)
    monkeypatch.setattr(bt, "get_buttons_state", lambda: {})
    return bt


class ClientSocket:
    def __init__(self, chunks=(), fail_send=False):
        self.chunks = iter(chunks)
        self.fail_send = fail_send
        self.closed = False
        self.sent = []
        self.recv_calls = 0
    def sendall(self, data):
        if self.fail_send:
            raise OSError("disconnected")
        self.sent.append(json.loads(data))
    def recv(self, amount):
        self.recv_calls += 1
        value = next(self.chunks, b"")
        if isinstance(value, Exception):
            raise value
        return value
    def settimeout(self, value):
        assert value > 0
    def close(self):
        self.closed = True


def test_bluetooth_closes_socket_on_initial_send_failure(bluetooth):
    sock = ClientSocket(fail_send=True)
    bluetooth.handle_client(sock, ("AA:BB", 1))
    assert sock.closed and sock.recv_calls == 0


def test_bluetooth_rejects_oversized_command_and_closes(bluetooth, monkeypatch):
    received = []
    monkeypatch.setattr(bluetooth, "process_command", received.append)
    sock = ClientSocket([b"x" * 1024] * 9 + [b"\n"])
    bluetooth.handle_client(sock, ("AA:BB", 1))
    assert sock.closed and not received and sock.recv_calls == 9


def test_bluetooth_handles_fragmented_json_command(bluetooth, monkeypatch):
    received = []
    monkeypatch.setattr(bluetooth, "process_command", lambda cmd: received.append(cmd) or {"ok": True})
    sock = ClientSocket([b'{"cmd":', b'"status"}\n'])
    bluetooth.handle_client(sock, ("AA:BB", 1))
    assert received == [{"cmd": "status"}] and sock.sent[-1] == {"ok": True}
    assert sock.closed


def test_bluetooth_idle_connection_closes_without_waiting(bluetooth, monkeypatch):
    clock = iter([0.0, bluetooth.IDLE_TIMEOUT + 1.0])
    monkeypatch.setattr(bluetooth.time, "monotonic", lambda: next(clock))
    sock = ClientSocket()
    bluetooth.handle_client(sock, ("AA:BB", 1))
    assert sock.closed and sock.recv_calls == 0


@pytest.mark.parametrize("mac,paired,window,expected", [
    ("aa:bb", ["AA:BB"], 0, True),
    ("CC:DD", ["AA:BB"], 0, False),
    ("CC:DD", [], 1, True),
])
def test_bluetooth_authorization(bluetooth, monkeypatch, mac, paired, window, expected):
    monkeypatch.setattr(bluetooth, "get_bt_privacy", lambda: {"paired_devices": paired, "pair_remaining_sec": window})
    assert bluetooth.authorized_client((mac, 1)) is expected


def test_bluetooth_session_releases_slot_even_on_handler_error(bluetooth, monkeypatch):
    slots = []
    monkeypatch.setattr(bluetooth, "_client_slots", types.SimpleNamespace(release=lambda: slots.append(1)))
    def fail(*args):
        raise OSError("disconnected")
    monkeypatch.setattr(bluetooth, "handle_client", fail)
    with pytest.raises(OSError):
        bluetooth._client_session(ClientSocket(), ("AA:BB", 1))
    assert slots == [1]


def test_stale_ticker_completion_cannot_override_newer_screen(bluetooth, monkeypatch):
    modes = []
    monkeypatch.setattr(bluetooth, "get_screen_mode", lambda: "ticker")
    monkeypatch.setattr(bluetooth, "set_screen_mode", modes.append)
    bluetooth._screen_generation = 2
    bluetooth.finish_ticker(1)
    assert modes == []
    bluetooth.finish_ticker(2)
    assert modes == ["clock"]



def test_delayed_ticker_worker_cannot_start_after_new_screen_command(bluetooth, monkeypatch):
    pending, commands = [], []
    mode = {"current": "clock"}
    monkeypatch.setattr(bluetooth, "get_screen_mode", lambda: mode["current"])
    monkeypatch.setattr(bluetooth, "set_screen_mode", lambda value: mode.update(current=value))
    monkeypatch.setattr(bluetooth.subprocess, "run", lambda cmd, **kw: commands.append(cmd))
    def fake_popen(cmd, **kwargs):
        commands.append(cmd)
        return types.SimpleNamespace(wait=lambda: 0)
    monkeypatch.setattr(bluetooth.subprocess, "Popen", fake_popen)
    class DeferredThread:
        def __init__(self, target, **kwargs):
            self.target = target
        def start(self):
            pending.append(self.target)
    monkeypatch.setattr(bluetooth.threading, "Thread", DeferredThread)
    bluetooth.process_command({"cmd": "set_screen", "mode": "ticker", "text": "old"})
    bluetooth.process_command({"cmd": "set_screen", "mode": "eyes", "state": "happy"})
    assert mode["current"] == "eyes"
    before = list(commands)
    pending.pop()()
    assert commands == before
    assert mode["current"] == "eyes"



def test_cancelled_voice_request_keeps_single_slot_until_worker_finishes(monkeypatch):
    ai = load_script("atriai", monkeypatch)
    release = ai.threading.Event()
    workers = []
    real_thread = ai.threading.Thread
    def tracked_thread(*args, **kwargs):
        thread = real_thread(*args, **kwargs)
        workers.append(thread)
        return thread
    monkeypatch.setattr(ai.threading, "Thread", tracked_thread)
    monkeypatch.setattr(ai, "consume_button_trigger", lambda: True)
    monkeypatch.setattr(ai, "is_mic_muted", lambda: False)
    monkeypatch.setattr(ai, "check_resume_visual", lambda: None)
    def pending_request():
        assert release.wait(2), "test failed to release worker"
        return "finished"
    try:
        with pytest.raises(InterruptedError):
            ai._voice_call(pending_request)
        with pytest.raises(RuntimeError, match="still finishing"):
            ai._voice_call(lambda: pytest.fail("second operation started"))
    finally:
        release.set()
        for worker in workers:
            worker.join(timeout=2)
            assert not worker.is_alive()
    monkeypatch.setattr(ai, "consume_button_trigger", lambda: False)
    assert ai._voice_call(lambda: "ready") == "ready"
    workers[-1].join(timeout=2)



@pytest.mark.parametrize("legacy,expected", [
    ("ws://example.test:8090/voice", "https://example.test:8090"),
    ("wss://example.test:8090/voice/", "https://example.test:8090"),
    ("http://example.test/api/", "https://example.test/api"),
    ("https://example.test/api", "https://example.test/api"),
])
def test_voice_migrates_legacy_transport_config_without_enabling(tmp_path, monkeypatch, legacy, expected):
    ai = load_script("atriai", monkeypatch)
    config = tmp_path / "ai.json"
    config.write_text(json.dumps({"server": legacy, "enabled": False}))
    monkeypatch.setattr(ai, "CONFIG_FILE", str(config))
    value = ai.load_config()
    assert value["server"] == expected and value["enabled"] is False


def test_light_frame_writers_respect_error_and_ring_ownership(tmp_path, monkeypatch):
    import glob
    monkeypatch.setattr(glob, "glob", lambda *a: [])
    light = load_script("light", monkeypatch)
    screen, ring = tmp_path / "screen", tmp_path / "ring"
    screen.write_bytes(b"old screen")
    ring.write_bytes(b"old ring")
    monkeypatch.setattr(light, "FB_SCREEN", str(screen))
    monkeypatch.setattr(light, "FB_RING", str(ring))
    monkeypatch.setattr(light, "active_error", lambda: {"code": 6})
    assert light.screen_send_frame(b"x" * 1200) is False
    assert light.ring_send_frame(b"x" * 72) is False
    assert screen.read_bytes() == b"old screen" and ring.read_bytes() == b"old ring"
    monkeypatch.setattr(light, "active_error", lambda: None)
    actual_exists = light.os.path.exists
    monkeypatch.setattr(light.os.path, "exists", lambda path: path == "/var/run/atrios/ring_override" or actual_exists(path))
    assert light.ring_send_frame(b"x" * 72) is False
    assert ring.read_bytes() == b"old ring"
    assert light.screen_send_frame(b"x" * 1200) is True
    assert screen.read_bytes() == b"x" * 1200


@pytest.mark.parametrize("error,mic_muted,override", [(True,False,False), (False,True,False), (False,False,True)])
def test_bluetooth_ring_cannot_preempt_safety_indicator(bluetooth, monkeypatch, error, mic_muted, override):
    monkeypatch.setattr(bluetooth, "active_error", lambda: {"code": 6} if error else None)
    monkeypatch.setattr(bluetooth, "get_buttons_state", lambda: {"mic_muted": mic_muted})
    actual_exists = bluetooth.os.path.exists
    monkeypatch.setattr(bluetooth.os.path, "exists", lambda path: override if path == "/var/run/atrios/ring_override" else actual_exists(path))
    monkeypatch.setattr(bluetooth.subprocess, "run", lambda *a, **k: pytest.fail("busy ring owner was killed"))
    monkeypatch.setattr(bluetooth.subprocess, "Popen", lambda *a, **k: pytest.fail("ring animation preempted safety indication"))
    assert bluetooth.process_command({"cmd": "set_ring", "mode": "rainbow"})["success"] is False
