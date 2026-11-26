"""Host bridge tests: no Telegram traffic, no real Codex tasks, no credentials."""
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time
import types

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import telegram_codex_bridge as bridge

OWNER = 7181456481
TOKEN = "12345678:" + "a" * 30
SESSION = "12345678-1234-1234-1234-123456789abc"


def config(tmp_path):
    state = tmp_path / "state"
    state.mkdir(exist_ok=True)
    return {"owner_id": OWNER, "codex": sys.executable, "workspace": str(tmp_path),
            "state_dir": str(state), "sandbox": "read-only", "token_file": str(tmp_path / "token")}


class TelegramStub:
    def __init__(self):
        self.messages = []
    def send(self, text):
        self.messages.append(text)


class RunnerStub:
    def __init__(self):
        self.stopped = 0
    def stop(self):
        self.stopped += 1


def update(text="hello", *, sender=OWNER, chat=OWNER, kind="private", id=1):
    return {"update_id": id, "message": {"from": {"id": sender}, "chat": {"id": chat, "type": kind}, "text": text, "date": 2000000000}}


@pytest.mark.parametrize("value", [update(sender=123), update(chat=123), update(kind="group"), update(sender=True),
    {"update_id": 1, "edited_message": update()["message"]}, {"update_id": 1}, update(text=None)])
def test_only_exact_owner_private_text_is_authorized(value):
    assert bridge.authorized_message(value, OWNER) is None
    assert bridge.authorized_message(update(), OWNER) == "hello"


def test_unauthorized_update_is_acknowledged_without_reply_or_job(tmp_path):
    client, runner = TelegramStub(), RunnerStub()
    bot = bridge.Bridge(config(tmp_path), client, runner)
    bot.handle_update(update(sender=123, id=8))
    assert bot.state["offset"] == 9 and not bot.state["queue"] and client.messages == []


def test_task_queue_and_offset_survive_restart_without_duplicate_replay(tmp_path):
    cfg, client = config(tmp_path), TelegramStub()
    bot = bridge.Bridge(cfg, client, RunnerStub())
    bot.handle_update(update("first", id=10))
    assert bot.state["queue"] == ["first"]
    # Simulate crash after the queue's durable write but before offset commit.
    saved = json.loads(bot.path.read_text())
    saved["offset"] = 0
    bridge.atomic_state(bot.path, saved)
    restored = bridge.Bridge(cfg, TelegramStub(), RunnerStub())
    restored.handle_update(update("first", id=10))
    assert restored.state["queue"] == ["first"] and restored.state["offset"] == 11


def test_stop_cancels_active_job_and_clears_pending_queue(tmp_path):
    runner = RunnerStub()
    bot = bridge.Bridge(config(tmp_path), TelegramStub(), runner)
    bot.handle_text("first")
    bot.active_cancel = threading.Event()
    bot.state["active"] = True
    bot.handle_text("/stop")
    assert bot.active_cancel.is_set() and runner.stopped == 1 and not bot.state["queue"]


def test_new_session_requires_idle_and_preserves_context_config(tmp_path):
    cfg, client = config(tmp_path), TelegramStub()
    cfg["context_file"] = str(tmp_path / "context.txt")
    bot = bridge.Bridge(cfg, client, RunnerStub())
    bot.remember_session(SESSION)
    bot.handle_text("/new")
    assert bot.state["session_id"] is None and cfg["context_file"].endswith("context.txt")
    bot.handle_text("job")
    bot.remember_session(SESSION)
    bot.handle_text("/new")
    assert bot.state["session_id"] == SESSION and "stop" in client.messages[-1]


def test_queue_limits_and_status_do_not_expose_prompts(tmp_path):
    client = TelegramStub()
    bot = bridge.Bridge(config(tmp_path), client, RunnerStub())
    for i in range(bridge.MAX_QUEUE + 1):
        bot.handle_text("private prompt " + str(i))
    assert len(bot.state["queue"]) == bridge.MAX_QUEUE and "\u0437\u0430\u043f\u043e\u043b\u043d\u0435\u043d\u0430" in client.messages[-1]
    bot.handle_text("x" * (bridge.MAX_PROMPT + 1))
    assert "\u0434\u043b\u0438\u043d\u043d\u0430\u044f" in client.messages[-1]
    bot.handle_text("/status")
    assert "private prompt" not in client.messages[-1]


def test_restart_does_not_rerun_interrupted_active_job(tmp_path):
    cfg = config(tmp_path)
    bridge.atomic_state(Path(cfg["state_dir"]) / "state.json", {"active": True, "session_id": SESSION})
    bot = bridge.Bridge(cfg, TelegramStub(), RunnerStub())
    assert bot.interrupted and not bot.state["active"] and bot.state["queue"] == []


def test_single_instance_lock_prevents_second_worker(tmp_path):
    with bridge.single_instance(tmp_path / "lock"):
        with pytest.raises(RuntimeError, match="already running"):
            with bridge.single_instance(tmp_path / "lock"):
                pytest.fail("second bridge acquired lock")
    with bridge.single_instance(tmp_path / "lock"):
        pass


@pytest.mark.parametrize("sandbox", ["read-only", "workspace-write"])
def test_codex_command_enforces_sandbox_and_stdin_prompt(tmp_path, sandbox):
    cfg = config(tmp_path)
    cfg["sandbox"] = sandbox
    args = bridge.Codex(cfg).command(SESSION)
    assert args[args.index("--sandbox") + 1] == sandbox
    assert args[args.index("-C") + 1] == str(tmp_path)
    assert args[-3:] == ["resume", SESSION, "-"]
    assert args[args.index("-a") + 1] == "never"
    assert "--dangerously-bypass-approvals-and-sandbox" not in args
    with pytest.raises(ValueError):
        bridge.Codex(cfg).command("--last")


def test_configuration_rejects_unsandboxed_mode(tmp_path):
    cfg = config(tmp_path)
    cfg["sandbox"] = "danger-full-access"
    path = tmp_path / "config.json"
    path.write_text(json.dumps(cfg))
    with pytest.raises(ValueError, match="sandbox"):
        bridge.load_config(path)
    cfg.pop("sandbox")
    path.write_text(json.dumps(cfg))
    assert bridge.load_config(path)["sandbox"] == "read-only"


def test_telegram_token_and_url_never_enter_argv_or_error_text(tmp_path, monkeypatch):
    token_file = tmp_path / "token"
    token_file.write_text(TOKEN)
    requests = []
    def invoke(args, **kwargs):
        requests.append((args, kwargs["input"].decode()))
        return types.SimpleNamespace(returncode=0, stdout=b'{"ok":true,"result":{}}')
    monkeypatch.setattr(bridge.subprocess, "run", invoke)
    client = bridge.Telegram(str(token_file), "curl.exe", OWNER)
    client.send('line1\n"quoted" ' + TOKEN)
    argv, body = requests[-1]
    assert TOKEN not in repr(argv) and "api.telegram.org" not in repr(argv)
    assert TOKEN in body  # URL is supplied privately on stdin.
    payload = json.loads(json.loads(next(line[7:] for line in body.splitlines() if line.startswith("data = "))))
    assert payload["chat_id"] == OWNER and TOKEN not in payload["text"]
    assert payload["text"].endswith("[REDACTED]")
    def fail(args, **kwargs):
        raise subprocess.TimeoutExpired(args, 1, stderr=("https://api.telegram.org/bot" + TOKEN).encode())
    monkeypatch.setattr(bridge.subprocess, "run", fail)
    with pytest.raises(RuntimeError) as caught:
        client.call("getMe")
    assert TOKEN not in str(caught.value) and "https://" not in str(caught.value)
    assert caught.value.__suppress_context__


def test_telegram_splitting_respects_utf16_limits():
    text = "\U0001f600" * 5000 + "tail"
    chunks = list(bridge.telegram_chunks(text))
    assert "".join(chunks) == text
    assert all(len(part.encode("utf-16-le")) // 2 <= 3500 for part in chunks)


def fake_codex(tmp_path, text):
    script = tmp_path / "fake-codex"
    script.write_text("#!/usr/bin/env python3\n" + text)
    script.chmod(0o700)
    return script


def test_real_process_json_parser_sends_only_final_and_seeds_new_thread_context(tmp_path):
    cfg = config(tmp_path)
    capture = tmp_path / "stdin.txt"
    cfg["context_file"] = str(tmp_path / "context.txt")
    Path(cfg["context_file"]).write_text("PRIVATE PROJECT HANDOFF")
    cfg["codex"] = str(fake_codex(tmp_path, f"""
import json, sys
from pathlib import Path
Path({str(capture)!r}).write_text(sys.stdin.read())
print(json.dumps({{"type":"thread.started","thread_id":{SESSION!r}}}))
print(json.dumps({{"type":"item.completed","item":{{"type":"command_execution","aggregated_output":"PRIVATE TOOL OUTPUT"}}}}))
print(json.dumps({{"type":"item.completed","item":{{"type":"agent_message","text":"commentary"}}}}))
print(json.dumps({{"type":"item.completed","item":{{"type":"agent_message","text":"final answer"}}}}))
print(json.dumps({{"type":"turn.completed"}}))
"""))
    runner, sessions = bridge.Codex(cfg), []
    assert runner.run("$(do-not-execute) & task", None, threading.Event(), sessions.append) == "final answer"
    assert sessions == [SESSION]
    data = capture.read_text()
    assert "PRIVATE PROJECT HANDOFF" in data and "$(do-not-execute) & task" in data
    assert runner.process is None
    assert runner.run("next task", SESSION, threading.Event(), sessions.append) == "final answer"
    assert "PRIVATE PROJECT HANDOFF" not in capture.read_text()


def test_oversized_private_context_fails_before_process_start(tmp_path):
    cfg = config(tmp_path)
    cfg["context_file"] = str(tmp_path / "context")
    Path(cfg["context_file"]).write_text("x" * 20481)
    with pytest.raises(ValueError, match="20 KiB"):
        bridge.Codex(cfg).run("task", None, threading.Event(), lambda session: None)


@pytest.mark.skipif(os.name == "nt", reason="POSIX fake executable; Windows actual CLI smoke is separate")
def test_stop_terminates_actual_job_process(tmp_path):
    cfg = config(tmp_path)
    cfg["codex"] = str(fake_codex(tmp_path, "import sys,time\nsys.stdin.read()\ntime.sleep(30)\n"))
    runner, cancel, results = bridge.Codex(cfg), threading.Event(), []
    def work():
        try:
            runner.run("task", None, cancel, lambda session: None)
        except InterruptedError:
            results.append("cancelled")
    worker = threading.Thread(target=work)
    worker.start()
    deadline = time.monotonic() + 3
    while runner.process is None and time.monotonic() < deadline:
        time.sleep(0.01)
    assert runner.process is not None
    cancel.set()
    runner.stop()
    worker.join(timeout=3)
    assert not worker.is_alive() and results == ["cancelled"] and runner.process is None


def test_worker_serializes_tasks_and_resumes_saved_session(tmp_path):
    client, runner = TelegramStub(), RunnerStub()
    bot = bridge.Bridge(config(tmp_path), client, runner)
    calls = []
    def run(prompt, session, cancel, on_session):
        assert bot.state["active"]
        calls.append((prompt, session))
        on_session(SESSION)
        if len(calls) == 2:
            bot.stopping.set()
        return "answer"
    runner.run = run
    bot.handle_text("first")
    bot.handle_text("second")
    bot.work()
    assert calls == [("first", None), ("second", SESSION)]
    assert not bot.state["active"] and bot.state["queue"] == []


def test_windows_restricted_token_fallback_is_explicit_and_validated(tmp_path):
    cfg = config(tmp_path)
    cfg["windows_sandbox"] = "unelevated"
    args = bridge.Codex(cfg).command()
    assert args[args.index("-c") + 1] == 'windows.sandbox="unelevated"'
    assert args[args.index("--sandbox") + 1] == "read-only"
    cfg["windows_sandbox"] = "disabled"
    path = tmp_path / "config.json"
    path.write_text(json.dumps(cfg))
    with pytest.raises(ValueError, match="Windows sandbox"):
        bridge.load_config(path)


def test_historical_owner_tasks_are_dropped_and_cutoff_is_persistent(tmp_path):
    cfg, client = config(tmp_path), TelegramStub()
    cfg["accept_after"] = 1000
    bot = bridge.Bridge(cfg, client, RunnerStub())
    old = update("old", id=1)
    old["message"]["date"] = 999
    bot.handle_update(old)
    assert not bot.state["queue"] and client.messages == [] and bot.state["offset"] == 2
    current = update("new", id=2)
    current["message"]["date"] = 1001
    bot.handle_update(current)
    assert bot.state["queue"] == ["new"]
    cfg["accept_after"] = 5000
    restored = bridge.Bridge(cfg, client, RunnerStub())
    assert restored.state["accepted_after"] == 1000
