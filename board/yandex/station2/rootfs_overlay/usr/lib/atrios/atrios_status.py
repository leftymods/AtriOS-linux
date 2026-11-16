"""Shared error state for the screen, LED ring and companion telemetry."""
import fcntl
import json
import math
import errno
import os
import tempfile
import time
import ssl
import urllib.error
from pathlib import Path

ERROR_FILE = Path("/var/run/atrios/errors.json")
ERROR_CODES = {"network": 1, "tls": 2, "server": 3, "audio": 4, "update": 5, "signature": 6, "storage": 7}


def error_kind(exc, default="update"):
    reason = getattr(exc, "reason", exc)
    if getattr(exc, "error_kind", None) in ERROR_CODES:
        return exc.error_kind
    if isinstance(reason, ssl.SSLError):
        return "tls"
    if isinstance(reason, OSError) and reason.errno in (errno.ENOSPC, errno.EIO, errno.EROFS, errno.EDQUOT):
        return "storage"
    if isinstance(exc, urllib.error.URLError) and not isinstance(exc, urllib.error.HTTPError):
        return "network"
    if isinstance(reason, (TimeoutError, ConnectionError)):
        return "network"
    return default


def get_errors():
    try:
        data = json.loads(ERROR_FILE.read_text())
        if not isinstance(data, dict):
            return {}
        now = time.time()
        return {k: v for k, v in data.items() if isinstance(v, dict) and isinstance(v.get("expires"), (int, float)) and v["expires"] > now and isinstance(v.get("code"), int) and 1 <= v["code"] <= 7 and isinstance(v.get("time"), (int, float)) and math.isfinite(v["time"]) and math.isfinite(v["expires"])}
    except (OSError, ValueError, TypeError):
        return {}


def active_error():
    errors = get_errors()
    return max(errors.values(), key=lambda v: (v.get("code", 0), v.get("time", 0)), default=None)


def set_error(source, kind=None, message="", ttl=120):
    """Set/clear one subsystem; missing display/state storage never breaks its caller."""
    try:
        ERROR_FILE.parent.mkdir(parents=True, exist_ok=True)
        with open(str(ERROR_FILE) + ".lock", "a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            data = get_errors()
            data.pop(source, None)
            if kind:
                now = time.time()
                data[source] = {"source": source, "kind": kind, "code": ERROR_CODES.get(kind, 5), "message": str(message)[:240], "time": now, "expires": now + max(1, min(ttl, 86400))}
            fd, name = tempfile.mkstemp(prefix=".errors-", dir=ERROR_FILE.parent)
            try:
                with os.fdopen(fd, "w") as out:
                    json.dump(data, out, ensure_ascii=False)
                os.replace(name, ERROR_FILE)
            finally:
                if os.path.exists(name):
                    os.unlink(name)
    except OSError:
        pass
