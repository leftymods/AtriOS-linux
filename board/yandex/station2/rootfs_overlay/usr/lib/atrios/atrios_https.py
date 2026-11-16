"""Bounded, certificate-verified outbound HTTPS for voice and updates."""
import json
import http.client
import socket
import ssl
import threading
import time
import urllib.parse
import urllib.error
import urllib.request


def require_https(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
        raise ValueError("An HTTPS URL without credentials or fragment is required")
    return url


class SecureRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        try:
            require_https(newurl)
        except ValueError:
            fp.close()
            raise
        redirected = super().redirect_request(req, fp, code, msg, headers, newurl)
        if redirected and urllib.parse.urlsplit(req.full_url).netloc != urllib.parse.urlsplit(newurl).netloc:
            redirected.remove_header("Authorization")
        return redirected


class DeadlineConnection(http.client.HTTPSConnection):
    """Abort TLS/header/body slow-drip reads at an absolute wall-clock deadline."""
    def __init__(self, *args, deadline, cancel=None, **kwargs):
        self.deadline = deadline
        self.cancel = cancel
        self.deadline_socket = None
        self.timer = None
        self.finished = threading.Event()
        super().__init__(*args, **kwargs)

    def connect(self):
        super().connect()
        self.deadline_socket = self.sock
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            self.abort()
            raise TimeoutError("HTTPS connection deadline exceeded")
        self.timer = threading.Thread(target=self.watch, daemon=True)
        self.timer.daemon = True
        self.timer.start()

    def watch(self):
        while not self.finished.is_set():
            remaining = self.deadline - time.monotonic()
            if remaining <= 0 or (self.cancel and self.cancel()):
                self.abort()
                return
            self.finished.wait(min(0.1, remaining))

    def abort(self):
        if self.deadline_socket:
            try:
                self.deadline_socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def finish(self):
        self.finished.set()


class DeadlineHandler(urllib.request.HTTPSHandler):
    def __init__(self, context, timeout, cancel=None):
        super().__init__(context=context)
        self.deadline = time.monotonic() + timeout
        self.connections = []
        self.cancel = cancel

    def https_open(self, req):
        def connection(*args, **kwargs):
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("HTTPS deadline exceeded")
            kwargs["timeout"] = min(kwargs.get("timeout", remaining), remaining)
            conn = DeadlineConnection(*args, deadline=self.deadline, cancel=self.cancel, **kwargs)
            self.connections.append(conn)
            return conn
        return self.do_open(connection, req, context=self._context, check_hostname=self._check_hostname)

    def finish(self):
        for conn in self.connections:
            conn.finish()


class ManagedResponse:
    def __init__(self, response, handler):
        self.response = response
        self.handler = handler

    def __getattr__(self, name):
        return getattr(self.response, name)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        try:
            self.response.close()
        finally:
            self.handler.finish()


def open_https(url, *, data=None, headers=None, timeout=25, total_timeout=None, ca_file=None, cancel=None):
    require_https(url)
    context = ssl.create_default_context(cafile=ca_file or None)
    handler = DeadlineHandler(context, total_timeout or timeout, cancel=cancel)
    opener = urllib.request.build_opener(SecureRedirect(), handler)
    req = urllib.request.Request(url, data=data, headers={"User-Agent": "AtriOS/1", **(headers or {})})
    try:
        return ManagedResponse(opener.open(req, timeout=timeout), handler)
    except BaseException as exc:
        if isinstance(exc, urllib.error.HTTPError):
            exc.close()
        handler.finish()
        raise


def read_limited(response, limit, *, cancel=None, deadline=None):
    declared = response.headers.get("Content-Length")
    if declared and (int(declared) < 0 or int(declared) > limit):
        raise ValueError("HTTPS response exceeds size limit")
    chunks = bytearray()
    while True:
        if cancel and cancel():
            raise InterruptedError("Request cancelled")
        if deadline is not None and time.monotonic() >= deadline:
            raise TimeoutError("HTTPS response deadline exceeded")
        chunk = getattr(response, "read1", response.read)(min(65536, limit + 1 - len(chunks)))
        if not chunk:
            if declared and len(chunks) != int(declared):
                raise ValueError("Truncated HTTPS response")
            return bytes(chunks)
        chunks.extend(chunk)
        if len(chunks) > limit:
            raise ValueError("HTTPS response exceeds size limit")


def request_bytes(url, *, data=None, headers=None, limit=8 * 1024 * 1024, timeout=25, ca_file=None, cancel=None):
    deadline = time.monotonic() + timeout
    if cancel and cancel():
        raise InterruptedError("Request cancelled")
    with open_https(url, data=data, headers=headers, timeout=timeout, ca_file=ca_file, cancel=cancel) as response:
        return read_limited(response, limit, cancel=cancel, deadline=deadline)


def request_json(url, *, payload=None, headers=None, limit=1024 * 1024, **kwargs):
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    result = request_bytes(url, data=body, headers={"Accept": "application/json", **({"Content-Type": "application/json"} if body else {}), **(headers or {})}, limit=limit, **kwargs)
    value = json.loads(result)
    if not isinstance(value, dict):
        raise ValueError("Expected a JSON object")
    return value
