#!/usr/bin/env python3
"""Owner-only Telegram bridge to the installed Codex CLI (host tool, not firmware).

Run on Windows so the existing ChatGPT login and working curl.exe transport are
used. Secrets belong in a private token file, never this repository or argv.
The configuration defaults to read-only until the local operator changes it.
"""
from __future__ import annotations

import argparse
import contextlib
import json
import logging
import os
from pathlib import Path
import re
import signal
import sys
import urllib.parse
import subprocess
import threading
import time
from typing import Callable

LOG = logging.getLogger("telegram-codex")
MAX_PROMPT = 24000
MAX_QUEUE = 20
SESSION_RE = re.compile(r"[0-9a-fA-F-]{36}")
METHODS = {"getMe", "getUpdates", "sendMessage", "sendChatAction"}


def atomic_state(path: Path, value: dict):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    if os.name != "nt":
        os.chmod(path, 0o600)


def private_directory(path: Path):
    path.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        identity = subprocess.check_output(["whoami"], stderr=subprocess.DEVNULL, text=True).strip()
        result = subprocess.run(["icacls", str(path), "/inheritance:r", "/grant:r", identity + ":(OI)(CI)F"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode:
            raise RuntimeError("Cannot restrict the bridge state directory ACL")
    else:
        os.chmod(path, 0o700)


def load_config(path: Path):
    cfg = json.loads(path.read_text(encoding="utf-8-sig"))
    required = ("owner_id", "token_file", "codex", "workspace", "state_dir")
    if not isinstance(cfg, dict) or any(key not in cfg for key in required):
        raise ValueError("Bridge configuration is incomplete")
    if type(cfg["owner_id"]) is not int or cfg["owner_id"] <= 0:
        raise ValueError("owner_id must be a positive integer")
    if cfg.get("sandbox", "read-only") not in ("read-only", "workspace-write"):
        raise ValueError("Only read-only or workspace-write sandbox is supported")
    cfg.setdefault("sandbox", "read-only")
    if cfg.get("windows_sandbox") not in (None, "elevated", "unelevated"):
        raise ValueError("Unsupported Windows sandbox backend")
    cfg.setdefault("curl", "curl.exe" if os.name == "nt" else "curl")
    for key in ("token_file", "codex", "workspace", "state_dir"):
        if not isinstance(cfg[key], str) or not cfg[key] or "\0" in cfg[key]:
            raise ValueError("Invalid configuration path: " + key)
    cfg.setdefault("backend", "native")
    if cfg["backend"] not in ("native", "wsl"):
        raise ValueError("Unsupported Codex backend")
    if not Path(cfg["workspace"]).is_dir():
        raise ValueError("Workspace does not exist")
    if cfg["backend"] == "native" and not Path(cfg["codex"]).is_file():
        raise ValueError("Codex executable does not exist")
    if cfg["backend"] == "wsl":
        if not re.fullmatch(r"[A-Za-z0-9._-]+", cfg.get("wsl_distribution", "")):
            raise ValueError("Invalid WSL distribution")
        for key in ("codex", "linux_workspace", "wsl_bridge_script"):
            value = cfg.get(key, "")
            if not isinstance(value, str) or not value.startswith("/") or "\0" in value:
                raise ValueError("Invalid WSL path: " + key)
    if cfg.get("proxy_url"):
        proxy = urllib.parse.urlsplit(cfg["proxy_url"])
        if proxy.scheme != "http" or proxy.hostname != "127.0.0.1" or not proxy.port or proxy.username or proxy.password or proxy.path not in ("", "/") or proxy.query or proxy.fragment:
            raise ValueError("Only a loopback HTTP CONNECT proxy is supported")
    return cfg


def curl_quote(value: str):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r") + '"'


def telegram_chunks(text: str, budget=3500):
    """Telegram's limit is measured in UTF-16 units, including non-BMP emoji."""
    chunk, size = [], 0
    for char in text:
        units = 2 if ord(char) > 0xFFFF else 1
        if size + units > budget:
            yield "".join(chunk)
            chunk, size = [], 0
        chunk.append(char)
        size += units
    if chunk:
        yield "".join(chunk)


class Telegram:
    def __init__(self, token_file: str, curl: str, owner_id: int):
        self._token = Path(token_file).read_text(encoding="ascii").strip()
        if not re.fullmatch(r"[0-9]{5,15}:[A-Za-z0-9_-]{20,200}", self._token):
            raise ValueError("Invalid Telegram token file")
        self.curl, self.owner_id = curl, owner_id
        self._send_lock = threading.Lock()

    def call(self, method: str, payload=None, *, poll=False):
        if method not in METHODS:
            raise ValueError("Unsupported Telegram method")
        # curl's argv contains neither the token nor the URL nor the message.
        # Never log raw curl stderr, HTTP bodies, or exceptions containing URLs.
        timeout = 40 if poll else 25
        config = "\n".join([
            "url = " + curl_quote("https://api.telegram.org/bot" + self._token + "/" + method),
            'request = "POST"', 'header = "Content-Type: application/json"',
            "data = " + curl_quote(json.dumps(payload or {}, ensure_ascii=False)),
        ]) + "\n"
        try:
            result = subprocess.run([self.curl, "--silent", "--show-error", "--fail-with-body",
                                     "--connect-timeout", "10", "--max-time", str(timeout),
                                     "--config", "-"], input=config.encode("utf-8"),
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout + 5)
        except (OSError, subprocess.TimeoutExpired):
            raise RuntimeError("Telegram transport unavailable") from None
        try:
            response = json.loads(result.stdout)
        except (ValueError, UnicodeError):
            raise RuntimeError("Telegram returned an invalid response") from None
        if not isinstance(response, dict) or not response.get("ok") or result.returncode:
            code = response.get("error_code", 0) if isinstance(response, dict) else 0
            raise RuntimeError("Telegram request rejected (code " + str(int(code or 0)) + ")")
        return response.get("result")

    def send(self, text: str):
        text = str(text).replace(self._token, "[REDACTED]")
        with self._send_lock:
            for chunk in telegram_chunks(text or "(empty response)"):
                self.call("sendMessage", {"chat_id": self.owner_id, "text": chunk,
                                          "disable_web_page_preview": True})


def authorized_message(update: dict, owner_id: int):
    message = update.get("message")
    if not isinstance(message, dict):
        return None
    sender, chat = message.get("from"), message.get("chat")
    if not isinstance(sender, dict) or not isinstance(chat, dict):
        return None
    if (type(sender.get("id")) is not int or type(chat.get("id")) is not int or
            sender["id"] != owner_id or chat["id"] != owner_id or chat.get("type") != "private"):
        return None
    text = message.get("text")
    return text if isinstance(text, str) else None



def process_identity(pid):
    try:
        path = Path("/proc") / str(pid)
        fields = (path / "stat").read_text().rsplit(")", 1)[1].split()
        return {"ppid": int(fields[1]), "pgrp": int(fields[2]), "start": fields[19], "uid": path.stat().st_uid}
    except (OSError, ValueError, IndexError):
        return None


def stop_owned_tree(pid, grace=2, expected_start=None):
    """Stop only this user's dedicated job and descendants, including setsid tools.

    Snapshot birth times before signalling the parent; a detached tool can be
    reparented during shutdown, so PPID alone is not a safe cancellation key.
    """
    root = process_identity(pid)
    if not root or root["uid"] != os.geteuid() or root["pgrp"] != pid or (expected_start is not None and root["start"] != expected_start):
        return
    snapshot = {}
    for entry in Path("/proc").iterdir():
        if entry.name.isdigit():
            identity = process_identity(int(entry.name))
            if identity and identity["uid"] == root["uid"]:
                snapshot[int(entry.name)] = identity
    owned = {pid: root}
    while True:
        added = {child: data for child, data in snapshot.items() if child not in owned and data["ppid"] in owned}
        if not added:
            break
        owned.update(added)
    groups = {child for child, data in owned.items() if data["pgrp"] == child}
    def signal_all(signum):
        for child, previous in reversed(list(owned.items())):
            current = process_identity(child)
            if current and current["uid"] == previous["uid"] and current["start"] == previous["start"]:
                try:
                    os.kill(child, signum)
                except ProcessLookupError:
                    pass
        for group in groups:
            try:
                os.killpg(group, signum)
            except ProcessLookupError:
                pass
    signal_all(signal.SIGTERM)
    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        if all(process_identity(child) is None for child in owned):
            return
        time.sleep(0.05)
    signal_all(signal.SIGKILL)


class Codex:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self._lock = threading.Lock()
        self.process = None
        self.linux_identity = None

    def command(self, session=None):
        args = [self.cfg["codex"], "--no-daemon", "-a", "never", "exec", "--sandbox",
                self.cfg["sandbox"], "--json", "--color", "never", "-C", self.cfg.get("linux_workspace", self.cfg["workspace"])]
        if self.cfg.get("windows_sandbox"):
            args[2:2] = ["-c", "windows.sandbox=" + json.dumps(self.cfg["windows_sandbox"])]
        if session:
            if not isinstance(session, str) or not SESSION_RE.fullmatch(session):
                raise ValueError("Invalid stored Codex session id")
            args.extend(["resume", session])
        args.append("-")
        if self.cfg.get("backend") == "wsl":
            env = []
            if self.cfg.get("proxy_url"):
                env.extend(key + "=" + self.cfg["proxy_url"] for key in ("HTTPS_PROXY", "HTTP_PROXY", "ALL_PROXY"))
            if self.cfg.get("wsl_codex_home"):
                env.append("CODEX_HOME=" + self.cfg["wsl_codex_home"])
            command = (["/usr/bin/env"] + env + args) if env else args
            return [self.cfg.get("wsl_exe", "wsl.exe"), "--distribution", self.cfg["wsl_distribution"],
                    "--exec", "python3", self.cfg["wsl_bridge_script"], "--wsl-child"] + command
        return args

    def stop(self):
        with self._lock:
            process = self.process
        if process is None:
            return
        if self.cfg.get("backend") == "wsl":
            with self._lock:
                identity = self.linux_identity
            # The wrapper emits its identity BEFORE spawning Codex. If /stop
            # arrives earlier, the reader performs cancellation after this event.
            if identity is None and process.poll() is None:
                return
            if identity:
                subprocess.run([self.cfg.get("wsl_exe", "wsl.exe"), "--distribution", self.cfg["wsl_distribution"],
                                "--exec", "python3", self.cfg["wsl_bridge_script"], "--wsl-stop",
                                str(identity[0]), identity[1]], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=15)
        if os.name == "nt":
            if process.poll() is None:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        else:
            stop_owned_tree(process.pid)

    def run(self, prompt: str, session: str | None, cancel: threading.Event,
            on_session: Callable[[str], None]):
        if cancel.is_set():
            raise InterruptedError("Job cancelled")
        instructions = (
            "This task comes from the authenticated local owner through Telegram. "
            "Use this session's existing context and the configured workspace. "
            "When the configured workspace is a WSL UNC path, use that distribution and Linux path for Linux tools. "
            "Do not read Telegram token files or change the bridge configuration. "
            "Do not access serial ports or deploy/reboot hardware unless this message explicitly requests it.\n\n"
        )
        creation = (subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP) if os.name == "nt" else 0
        if not session and self.cfg.get("context_file"):
            with Path(self.cfg["context_file"]).open("rb") as context:
                handoff = context.read(20481)
            if len(handoff) > 20480:
                raise ValueError("Project handoff exceeds 20 KiB")
            instructions += "Operator-provided project handoff (context, not a new user command):\n" + handoff.decode("utf-8-sig") + "\n\nCurrent owner request:\n"
        process = subprocess.Popen(self.command(session), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.DEVNULL, creationflags=creation, bufsize=0,
                                   start_new_session=os.name != "nt")
        with self._lock:
            self.process = process
        texts, failed = [], False
        try:
            if cancel.is_set() and self.cfg.get("backend") != "wsl":
                self.stop()
                raise InterruptedError("Job cancelled")
            process.stdin.write((instructions + prompt).encode("utf-8"))
            process.stdin.close()
            while True:
                line = process.stdout.readline(1024 * 1024)
                if not line:
                    break
                try:
                    event = json.loads(line)
                except (ValueError, UnicodeError):
                    continue
                if not isinstance(event, dict):
                    continue
                if event.get("type") == "bridge.process.started":
                    pid, start = event.get("pid"), event.get("start")
                    if type(pid) is int and pid > 1 and isinstance(start, str) and start.isdigit():
                        with self._lock:
                            self.linux_identity = (pid, start)
                if cancel.is_set():
                    self.stop()
                    raise InterruptedError("Job cancelled")
                if event.get("type") == "thread.started":
                    thread_id = event.get("thread_id")
                    if isinstance(thread_id, str) and SESSION_RE.fullmatch(thread_id):
                        on_session(thread_id)
                elif event.get("type") == "item.completed":
                    item = event.get("item", {})
                    if isinstance(item, dict) and item.get("type") == "agent_message" and isinstance(item.get("text"), str):
                        texts.append(item["text"][:64000])
                        texts = texts[-8:]
                elif event.get("type") in ("turn.failed", "error"):
                    failed = True
            code = process.wait()
            if cancel.is_set():
                raise InterruptedError("Job cancelled")
            if code or failed:
                raise RuntimeError("Codex job failed; inspect Codex locally for details")
            if not texts:
                raise RuntimeError("Codex returned no assistant message")
            # CLI emits commentary as agent_message too. Send only the last
            # assistant message after a successful completed turn.
            return texts[-1]
        finally:
            if process.poll() is None:
                self.stop()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
            for pipe in (process.stdin, process.stdout):
                if pipe and not pipe.closed:
                    pipe.close()
            with self._lock:
                self.process = None
                self.linux_identity = None


class Bridge:
    def __init__(self, cfg: dict, telegram: Telegram, runner: Codex):
        self.cfg, self.telegram, self.runner = cfg, telegram, runner
        self.path = Path(cfg["state_dir"]) / "state.json"
        self.lock = threading.Lock()
        self.ready = threading.Event()
        self.stopping = threading.Event()
        self.active_cancel = None
        self.state = {"offset": 0, "session_id": None, "queue": [], "active": False, "last_task_update": -1,
                      "accepted_after": cfg.get("accept_after", time.time())}
        if self.path.exists():
            value = json.loads(self.path.read_text(encoding="utf-8"))
            if not isinstance(value, dict):
                raise ValueError("Invalid bridge state")
            self.state.update(value)
        if type(self.state["offset"]) is not int or not isinstance(self.state["queue"], list):
            raise ValueError("Invalid bridge offset or queue")
        if len(self.state["queue"]) > MAX_QUEUE or any(not isinstance(p, str) or len(p.encode("utf-8")) > MAX_PROMPT for p in self.state["queue"]):
            raise ValueError("Invalid queued jobs")
        # Never automatically replay a possibly destructive interrupted active job.
        self.interrupted = bool(self.state.get("active"))
        self.state["active"] = False
        self.save()
        if self.state["queue"]:
            self.ready.set()

    def save(self):
        atomic_state(self.path, self.state)

    def reply(self, text):
        try:
            self.telegram.send(text)
        except RuntimeError:
            LOG.warning("Owner notification could not be delivered")

    def remember_session(self, session):
        with self.lock:
            self.state["session_id"] = session
            self.save()

    def handle_update(self, update):
        if not isinstance(update, dict) or type(update.get("update_id")) is not int:
            return
        with self.lock:
            if update["update_id"] < self.state["offset"]:
                return
        text = authorized_message(update, self.cfg["owner_id"])
        message_date = update.get("message", {}).get("date") if isinstance(update.get("message"), dict) else None
        if text is not None and isinstance(message_date, (int, float)) and message_date >= self.state["accepted_after"]:
            self.handle_text(text, update_id=update["update_id"])
        with self.lock:
            self.state["offset"] = update["update_id"] + 1
            self.save()

    def handle_text(self, text, update_id=None):
        text = text.strip()
        command = text.split(None, 1)[0].split("@", 1)[0] if text else ""
        if command in ("/start", "/help"):
            self.reply("Codex: \u043e\u0442\u0434\u0435\u043b\u044c\u043d\u044b\u0439 \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u0435\u043c\u044b\u0439 \u0434\u0438\u0430\u043b\u043e\u0433 \u0441 \u043a\u043e\u043d\u0442\u0435\u043a\u0441\u0442\u043e\u043c \u043f\u0440\u043e\u0435\u043a\u0442\u0430. \u041f\u0440\u0438\u0448\u043b\u0438\u0442\u0435 \u0437\u0430\u0434\u0430\u0447\u0443 \u0442\u0435\u043a\u0441\u0442\u043e\u043c. /status /stop /new\n\u041f\u0440\u043e\u0435\u043a\u0442: " + self.cfg["workspace"] + "\n\u0420\u0435\u0436\u0438\u043c: " + self.cfg["sandbox"])
        elif command == "/status":
            with self.lock:
                summary = {key: self.state[key] for key in ("active", "session_id")}
                summary["queued"] = len(self.state["queue"])
                summary["sandbox"] = self.cfg["sandbox"]
                summary["paused"] = bool(self.cfg.get("pause_file") and Path(self.cfg["pause_file"]).exists())
            self.reply("Codex: " + ("\u0440\u0430\u0431\u043e\u0442\u0430\u0435\u0442" if summary["active"] else "\u043e\u0436\u0438\u0434\u0430\u043d\u0438\u0435") + "\n\u0417\u0430\u0434\u0430\u0447 \u0432 \u043e\u0447\u0435\u0440\u0435\u0434\u0438: " + str(summary["queued"]) + "\n\u0420\u0435\u0436\u0438\u043c: " + summary["sandbox"] + ("\n\u0417\u0430\u043f\u0443\u0441\u043a \u043f\u0440\u0438\u043e\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d \u043e\u043f\u0435\u0440\u0430\u0442\u043e\u0440\u043e\u043c." if summary["paused"] else ""))
        elif command == "/stop":
            with self.lock:
                pending = len(self.state["queue"])
                self.state["queue"] = []
                if self.active_cancel:
                    self.active_cancel.set()
                self.save()
            self.runner.stop()
            self.reply("\u041e\u0441\u0442\u0430\u043d\u043e\u0432\u043a\u0430 \u0437\u0430\u043f\u0440\u043e\u0448\u0435\u043d\u0430. \u0423\u0434\u0430\u043b\u0435\u043d\u043e \u0437\u0430\u0434\u0430\u0447 \u0438\u0437 \u043e\u0447\u0435\u0440\u0435\u0434\u0438: " + str(pending))
        elif command == "/new":
            with self.lock:
                busy = self.state["active"] or bool(self.state["queue"])
                if not busy:
                    self.state["session_id"] = None
                    self.save()
            self.reply("\u0421\u043d\u0430\u0447\u0430\u043b\u0430 \u043e\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u0435 \u0442\u0435\u043a\u0443\u0449\u0443\u044e \u0437\u0430\u0434\u0430\u0447\u0443: /stop." if busy else "\u0421\u043b\u0435\u0434\u0443\u044e\u0449\u0430\u044f \u0437\u0430\u0434\u0430\u0447\u0430 \u043d\u0430\u0447\u043d\u0451\u0442 \u043d\u043e\u0432\u044b\u0439 \u0434\u0438\u0430\u043b\u043e\u0433 Codex. \u041a\u043e\u043d\u0442\u0435\u043a\u0441\u0442 \u043f\u0440\u043e\u0435\u043a\u0442\u0430 \u0441\u043e\u0445\u0440\u0430\u043d\u0451\u043d.")
        elif command.startswith("/"):
            self.reply("\u041d\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043d\u0430\u044f \u043a\u043e\u043c\u0430\u043d\u0434\u0430. \u0421\u043f\u0440\u0430\u0432\u043a\u0430: /help.")
        elif not text:
            return
        elif len(text.encode("utf-8")) > MAX_PROMPT:
            self.reply("\u0417\u0430\u0434\u0430\u0447\u0430 \u0441\u043b\u0438\u0448\u043a\u043e\u043c \u0434\u043b\u0438\u043d\u043d\u0430\u044f: \u043c\u0430\u043a\u0441\u0438\u043c\u0443\u043c 24000 \u0431\u0430\u0439\u0442 UTF-8.")
        else:
            with self.lock:
                if update_id is not None and update_id <= self.state["last_task_update"]:
                    return
                full = len(self.state["queue"]) >= MAX_QUEUE
                if not full:
                    self.state["queue"].append(text)
                    if update_id is not None:
                        self.state["last_task_update"] = update_id
                    self.save()
                    number = len(self.state["queue"])
                    self.ready.set()
            self.reply("\u041e\u0447\u0435\u0440\u0435\u0434\u044c \u0437\u0430\u043f\u043e\u043b\u043d\u0435\u043d\u0430." if full else "\u0417\u0430\u0434\u0430\u0447\u0430 \u0434\u043e\u0431\u0430\u0432\u043b\u0435\u043d\u0430 \u0432 \u043e\u0447\u0435\u0440\u0435\u0434\u044c: " + str(number) + " (" + self.cfg["sandbox"] + ")")

    def work(self):
        while not self.stopping.is_set():
            self.ready.wait(0.5)
            if self.cfg.get("pause_file") and Path(self.cfg["pause_file"]).exists():
                self.stopping.wait(0.5)
                continue
            with self.lock:
                if not self.state["queue"] or self.stopping.is_set():
                    self.ready.clear()
                    continue
                prompt = self.state["queue"].pop(0)
                self.state["active"] = True
                self.active_cancel = threading.Event()
                cancel = self.active_cancel
                session = self.state["session_id"]
                self.save()
            self.reply("Codex \u0432\u044b\u043f\u043e\u043b\u043d\u044f\u0435\u0442 \u0437\u0430\u0434\u0430\u0447\u0443 (" + self.cfg["sandbox"] + ").")
            try:
                answer = self.runner.run(prompt, session, cancel, self.remember_session)
                self.reply(answer)
            except InterruptedError:
                self.reply("\u0417\u0430\u0434\u0430\u0447\u0430 Codex \u043e\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d\u0430.")
            except Exception:
                LOG.warning("Codex job failed; raw output is intentionally not logged")
                self.reply("Codex \u043d\u0435 \u0441\u043c\u043e\u0433 \u0437\u0430\u0432\u0435\u0440\u0448\u0438\u0442\u044c \u0437\u0430\u0434\u0430\u0447\u0443. \u041f\u0440\u043e\u0432\u0435\u0440\u044c\u0442\u0435 \u043b\u043e\u043a\u0430\u043b\u044c\u043d\u044b\u0439 Codex \u0438 /status.")
            finally:
                with self.lock:
                    self.state["active"] = False
                    self.active_cancel = None
                    self.save()

    def run(self):
        identity = self.telegram.call("getMe")
        if not isinstance(identity, dict) or not identity.get("is_bot"):
            raise RuntimeError("Telegram account is not a bot")
        if self.interrupted:
            self.reply("\u041f\u0440\u0435\u0434\u044b\u0434\u0443\u0449\u0430\u044f \u0437\u0430\u0434\u0430\u0447\u0430 \u0431\u044b\u043b\u0430 \u043f\u0440\u0435\u0440\u0432\u0430\u043d\u0430. \u041f\u043e\u0432\u0442\u043e\u0440\u043d\u043e \u043d\u0435 \u0437\u0430\u043f\u0443\u0441\u043a\u0430\u043b\u0430\u0441\u044c. \u041f\u0440\u0438\u0448\u043b\u0438\u0442\u0435 \u043d\u043e\u0432\u0443\u044e \u0437\u0430\u0434\u0430\u0447\u0443 \u0434\u043b\u044f \u043f\u0440\u043e\u0434\u043e\u043b\u0436\u0435\u043d\u0438\u044f.")
        worker = threading.Thread(target=self.work, daemon=True)
        worker.start()
        try:
            while not self.stopping.is_set():
                try:
                    updates = self.telegram.call("getUpdates", {"offset": self.state["offset"], "timeout": 25,
                                                               "allowed_updates": ["message"]}, poll=True)
                    if not isinstance(updates, list):
                        raise RuntimeError("Invalid Telegram updates")
                    for update in updates:
                        self.handle_update(update)
                except RuntimeError:
                    LOG.warning("Telegram polling failed; retrying without logging credentials")
                    self.stopping.wait(5)
        except KeyboardInterrupt:
            pass
        finally:
            self.stopping.set()
            self.ready.set()
            with self.lock:
                if self.active_cancel:
                    self.active_cancel.set()
            self.runner.stop()
            worker.join(timeout=20)



@contextlib.contextmanager
def single_instance(path: Path):
    with path.open("a+b") as lock:
        lock.seek(0, 2)
        if lock.tell() == 0:
            lock.write(b"0")
            lock.flush()
        lock.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise RuntimeError("Another bridge instance is already running") from None
        try:
            yield
        finally:
            if os.name == "nt":
                import msvcrt
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_UN)



@contextlib.contextmanager
def managed_proxy(cfg):
    command = cfg.get("proxy_command")
    if not command:
        yield
        return
    if not isinstance(command, list) or not command or any(not isinstance(a, str) or "\0" in a for a in command):
        raise ValueError("Invalid proxy startup command")
    ready_file = Path(cfg["proxy_ready_file"])
    started = time.time()
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, creationflags=flags)
    identity = None
    try:
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("Local CONNECT proxy could not start")
            try:
                value = json.loads(ready_file.read_text())
                if (value.get("created", 0) >= started and type(value.get("pid")) is int and value["pid"] > 1
                        and isinstance(value.get("start"), str) and value["start"].isdigit()
                        and value.get("port") == urllib.parse.urlsplit(cfg["proxy_url"]).port):
                    identity = value
                    break
            except (OSError, ValueError, TypeError):
                pass
            time.sleep(0.1)
        if identity is None:
            raise RuntimeError("Local CONNECT proxy startup deadline exceeded")
        yield
    finally:
        if identity:
            subprocess.run([cfg.get("wsl_exe", "wsl.exe"), "--distribution", cfg["wsl_distribution"],
                            "--exec", "python3", cfg["wsl_bridge_script"], "--wsl-stop",
                            str(identity["pid"]), identity["start"]], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=15)
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="Validate local paths/configuration without contacting Telegram or Codex")
    args = parser.parse_args()
    cfg = load_config(args.config)
    private_directory(Path(cfg["state_dir"]))
    if args.check:
        print(json.dumps({"owner_id": cfg["owner_id"], "workspace": cfg["workspace"], "sandbox": cfg["sandbox"]}))
        return
    client = Telegram(cfg["token_file"], cfg["curl"], cfg["owner_id"])
    with single_instance(Path(cfg["state_dir"]) / "bridge.lock"), managed_proxy(cfg):
        Bridge(cfg, client, Codex(cfg)).run()



def wsl_child(command):
    if not command:
        return 2
    if os.getpgrp() != os.getpid():
        os.setsid()
    identity = process_identity(os.getpid())
    print(json.dumps({"type": "bridge.process.started", "pid": os.getpid(), "start": identity["start"]}), flush=True)
    process = subprocess.Popen(command, stdin=sys.stdin.buffer, stdout=sys.stdout.buffer, stderr=sys.stderr.buffer)
    return process.wait()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--wsl-child":
        raise SystemExit(wsl_child(sys.argv[2:]))
    if len(sys.argv) == 4 and sys.argv[1] == "--wsl-stop":
        if sys.argv[2].isdigit() and sys.argv[3].isdigit():
            stop_owned_tree(int(sys.argv[2]), expected_start=sys.argv[3])
            raise SystemExit(0)
        raise SystemExit(2)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        main()
    except Exception:
        LOG.error("Bridge startup failed; inspect configuration and local authentication. Secrets are not logged.")
        raise SystemExit(1)
