"""Real urllib/TLS integration using only an ephemeral 127.0.0.1 server."""
import contextlib
import http.client
import http.server
import json
import shutil
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
from pathlib import Path

import pytest

RUNTIME = (Path(__file__).resolve().parents[1] / "board/yandex/station2/rootfs_overlay/usr/lib/atrios")
sys.path.insert(0, str(RUNTIME))
import atrios_https as https


@pytest.fixture
def local_tls(tmp_path):
    openssl = shutil.which("openssl")
    if not openssl:
        pytest.skip("openssl unavailable for ephemeral TLS test certificate")
    cert, key = tmp_path / "cert.pem", tmp_path / "key.pem"
    subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", str(key), "-out", str(cert), "-days", "1",
                    "-subj", "/CN=localhost",
                    "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"],
                   check=True, capture_output=True, timeout=15)
    stop = threading.Event()
    requests = []
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        def log_message(self, *args):
            pass
        def do_GET(self):
            requests.append(("GET", self.path, dict(self.headers)))
            try:
                if self.path == "/redirect":
                    self.send_response(302)
                    self.send_header("Location", "http://127.0.0.1:1/insecure")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif self.path == "/http-error":
                    self.send_response(404)
                    self.send_header("Content-Length", "5")
                    self.end_headers()
                    self.wfile.write(b"error")
                elif self.path == "/slow-headers":
                    header = b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"
                    for byte in header:
                        if stop.wait(0.025):
                            break
                        self.connection.sendall(bytes([byte]))
                elif self.path == "/slow-body":
                    self.send_response(200)
                    self.send_header("Content-Length", "100")
                    self.end_headers()
                    for _ in range(100):
                        if stop.wait(0.025):
                            break
                        self.wfile.write(b"x")
                        self.wfile.flush()
                else:
                    body = b'{"ok":true}'
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError, OSError):
                pass
        def do_POST(self):
            body = self.rfile.read(int(self.headers["Content-Length"]))
            requests.append(("POST", self.path, dict(self.headers), body))
            response = json.dumps({"echo": json.loads(body)}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)
    class Server(http.server.ThreadingHTTPServer):
        daemon_threads = True
        allow_reuse_address = True
    server = Server(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=lambda: server.serve_forever(poll_interval=0.01), daemon=True)
    thread.start()
    try:
        yield "https://127.0.0.1:" + str(server.server_address[1]), str(cert), requests
    finally:
        stop.set()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
        assert not thread.is_alive()


@pytest.fixture
def connections(monkeypatch):
    created = []
    base = https.DeadlineConnection
    class TrackedConnection(base):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            created.append(self)
    monkeypatch.setattr(https, "DeadlineConnection", TrackedConnection)
    yield created
    for conn in created:
        assert conn.finished.is_set(), "HTTP handler did not finish its connection watcher"
        if conn.timer:
            conn.timer.join(timeout=1)
            assert not conn.timer.is_alive(), "deadline watcher leaked"
        if conn.deadline_socket:
            assert conn.deadline_socket.fileno() == -1, "client TLS socket leaked"


def test_verified_get_and_json_post(local_tls, connections):
    base, ca, requests = local_tls
    assert https.request_json(base + "/ok", ca_file=ca, timeout=2) == {"ok": True}
    assert https.request_json(base + "/echo", payload={"text": "hello"}, ca_file=ca, timeout=2) == {"echo": {"text": "hello"}}
    assert [item[0] for item in requests] == ["GET", "POST"]
    assert len(connections) == 2


def test_untrusted_certificate_is_rejected(local_tls, connections):
    base, _, requests = local_tls
    with pytest.raises(urllib.error.URLError) as caught:
        https.request_bytes(base + "/ok", timeout=2)
    assert isinstance(caught.value.reason, ssl.SSLCertVerificationError)
    assert not requests


def test_https_redirect_cannot_downgrade(local_tls, connections):
    base, ca, requests = local_tls
    with pytest.raises(ValueError, match="HTTPS"):
        https.request_bytes(base + "/redirect", ca_file=ca, timeout=2)
    assert len(requests) == 1


@pytest.mark.parametrize("path", ["/slow-body", "/slow-headers"])
def test_absolute_deadline_aborts_header_or_body_slow_drip(local_tls, connections, path):
    base, ca, _ = local_tls
    started = time.monotonic()
    with pytest.raises((OSError, ValueError, urllib.error.URLError, http.client.HTTPException)):
        https.request_bytes(base + path, ca_file=ca, timeout=0.2)
    elapsed = time.monotonic() - started
    assert elapsed < 1.0, f"slow-drip escaped deadline: {elapsed:.3f}s"


def test_cancel_predicate_aborts_blocked_header_read(local_tls, connections):
    base, ca, _ = local_tls
    started = time.monotonic()
    with pytest.raises((OSError, ValueError, urllib.error.URLError, http.client.HTTPException)):
        https.request_bytes(base + "/slow-headers", ca_file=ca, timeout=2,
                            cancel=lambda: time.monotonic() - started > 0.15)
    assert time.monotonic() - started < 1.0


def test_repeated_requests_leave_no_watchers_or_client_sockets(local_tls, connections):
    base, ca, _ = local_tls
    for _ in range(5):
        assert https.request_json(base + "/ok", ca_file=ca, timeout=2)["ok"] is True
    assert len(connections) == 5


def test_http_error_releases_response_and_socket(local_tls, connections):
    base, ca, _ = local_tls
    with pytest.raises(urllib.error.HTTPError) as caught:
        https.request_bytes(base + "/http-error", ca_file=ca, timeout=2)
    assert caught.value.code == 404
    assert caught.value.fp is None or caught.value.fp.closed
