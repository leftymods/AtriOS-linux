#!/usr/bin/env python3
"""WSL-loopback CONNECT proxy with a Windows TCP dialer for split-VPN hosts.

TLS is never terminated or inspected. No Windows listener is opened. Only the
listed OpenAI API/auth hosts on port 443 are accepted. This is a host admin tool.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import socketserver
import subprocess
import threading
import time

ALLOWED_HOSTS = frozenset({"chatgpt.com", "api.openai.com", "auth.openai.com", "ab.chatgpt.com"})
WINDOWS_DIALER = r"""
import socket,sys,threading,os,msvcrt
msvcrt.setmode(0,os.O_BINARY);msvcrt.setmode(1,os.O_BINARY)
s=socket.create_connection((sys.argv[1],443),timeout=15);s.settimeout(None)
sys.stdout.buffer.write(b'OK\n');sys.stdout.buffer.flush()
def receive():
 try:
  while True:
   data=s.recv(65536)
   if not data:break
   sys.stdout.buffer.write(data);sys.stdout.buffer.flush()
 except OSError:pass
 os._exit(0)
threading.Thread(target=receive,daemon=True).start()
try:
 while True:
  data=os.read(0,65536)
  if not data:break
  s.sendall(data)
except OSError:pass
try:s.shutdown(socket.SHUT_RDWR)
except OSError:pass
s.close()
"""


def allowed_target(target):
    host, separator, port = target.rpartition(":")
    return bool(separator and port == "443" and host.lower() in ALLOWED_HOSTS)


def make_handler(windows_python):
    slots = threading.BoundedSemaphore(8)
    class Handler(socketserver.StreamRequestHandler):
        def handle(self):
            if not slots.acquire(blocking=False):
                return
            try:
                self.proxy()
            except (OSError, ValueError, UnicodeError):
                pass
            finally:
                slots.release()

        def proxy(self):
            self.connection.settimeout(120)
            first = self.rfile.readline(8193)
            if len(first) > 8192:
                return
            words = first.decode("ascii").strip().split()
            if len(words) != 3 or words[0] != "CONNECT" or not allowed_target(words[1]):
                self.wfile.write(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
                return
            for _ in range(64):
                line = self.rfile.readline(8193)
                if not line or len(line) > 8192:
                    return
                if line in (b"\r\n", b"\n"):
                    break
            else:
                return
            host = words[1].rsplit(":", 1)[0]
            process = subprocess.Popen([windows_python, "-u", "-c", WINDOWS_DIALER, host],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.DEVNULL, bufsize=0)
            try:
                if process.stdout.readline(32) != b"OK\n":
                    return
                self.wfile.write(b"HTTP/1.1 200 Connection Established\r\n\r\n")
                self.wfile.flush()
                def receive():
                    try:
                        while True:
                            data = process.stdout.read(65536)
                            if not data:
                                break
                            self.connection.sendall(data)
                    except OSError:
                        pass
                    try:
                        self.connection.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                thread = threading.Thread(target=receive, daemon=True)
                thread.start()
                try:
                    while True:
                        data = self.connection.recv(65536)
                        if not data:
                            break
                        process.stdin.write(data)
                        process.stdin.flush()
                except OSError:
                    pass
            finally:
                process.stdin.close()
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                process.stdout.close()

    return Handler


class LoopbackServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows-python", required=True)
    parser.add_argument("--port", type=int, default=45267)
    parser.add_argument("--state-file", type=Path, required=True)
    args = parser.parse_args()
    if os.name == "nt" or not Path(args.windows_python).is_file() or not 0 <= args.port <= 65535:
        raise SystemExit("Run inside WSL with a valid Windows Python executable")
    if os.getpgrp() != os.getpid():
        os.setsid()
    from telegram_codex_bridge import atomic_state, process_identity
    identity = process_identity(os.getpid())
    stopped = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stopped.set())
    signal.signal(signal.SIGINT, lambda *_: stopped.set())
    args.state_file.parent.mkdir(parents=True, exist_ok=True)
    with LoopbackServer(("127.0.0.1", args.port), make_handler(args.windows_python)) as server:
        atomic_state(args.state_file, {"pid": os.getpid(), "start": identity["start"],
                                     "port": server.server_address[1], "created": time.time()})
        worker = threading.Thread(target=lambda: server.serve_forever(poll_interval=0.2), daemon=True)
        worker.start()
        try:
            stopped.wait()
        finally:
            server.shutdown()
            worker.join(timeout=2)
            try:
                state = json.loads(args.state_file.read_text())
                if state.get("pid") == os.getpid() and state.get("start") == identity["start"]:
                    args.state_file.unlink()
            except (OSError, ValueError):
                pass


if __name__ == "__main__":
    main()
