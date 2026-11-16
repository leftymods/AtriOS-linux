#!/usr/bin/env python3
"""Run a local AtriAI backend as a TLS-only, authenticated HTTP voice service.

Example (use the backend virtual environment, not the system Python):
  python tools/run_voice_https.py --backend-dir /path/to/AtriAI-repo \
    --cert /path/to/server.pem --key /path/to/server.key \
    --host 127.0.0.1 --port 8443 --token-file /path/to/token

Models are lazy by default. --warmup explicitly loads cached models at startup.
GigaAM and Silero loaders are restricted to existing local weights: missing
assets fail instead of downloading. No WebSocket or unencrypted listener exists.
The backend's llama.cpp HTTP services must be started separately.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import importlib
import ipaddress
import logging
import os
from pathlib import Path
import re
import ssl
import sys
import threading
import types

from aiohttp import web


ROUTES = (("POST", "/asr", "asr_endpoint"), ("POST", "/chat", "chat"),
          ("POST", "/tts", "tts_endpoint"), ("GET", "/health", "health"))


def load_backend(directory):
    directory = Path(directory).expanduser().resolve()
    package = directory / "server" if (directory / "server/app.py").is_file() else directory
    if not (package / "app.py").is_file():
        raise ValueError("--backend-dir must contain server/app.py or app.py")
    name = "_atri_voice_backend_" + hashlib.sha256(str(package).encode()).hexdigest()[:16]
    if name + ".app" in sys.modules:
        return sys.modules[name + ".app"]
    # A private package namespace keeps unrelated installed 'server' modules out.
    module = types.ModuleType(name)
    module.__path__ = [str(package)]
    module.__package__ = name
    sys.modules[name] = module
    try:
        return importlib.import_module(name + ".app")
    except BaseException:
        for key in list(sys.modules):
            if key == name or key.startswith(name + "."):
                del sys.modules[key]
        raise


def local_only_models(backend):
    """Adapt known backend loaders in memory; never edit the external checkout.

    Import torch/GigaAM only on the first actual inference or explicit warmup.
    GigaAM's cache gate and Silero's direct local PackageImporter avoid both
    checkpoint downloads and torch.hub repository/metadata refreshes.
    """
    asr = getattr(backend, "asr", None)
    tts = getattr(backend, "tts", None)
    if asr is not None and not getattr(asr, "_https_local_only", False):
        original_load = asr.load
        asr_lock = threading.Lock()
        def load_asr(name="ctc"):
            with asr_lock:
                import gigaam
                if not hasattr(gigaam, "_download_file"):
                    raise RuntimeError("Unsupported GigaAM cache API; refusing model downloads")
                def cached_only(_url, filename):
                    path = Path(filename)
                    if not path.is_file() or not path.stat().st_size:
                        raise RuntimeError("Provision GigaAM checkpoint locally before inference: " + str(path))
                    return str(path)
                gigaam._download_file = cached_only
                return original_load(name)
        asr.load = load_asr
        asr._https_local_only = True
    if tts is not None and not getattr(tts, "_https_local_only", False):
        tts_lock = threading.Lock()
        def load_silero():
            with tts_lock:
                if tts._silero is not None:
                    return tts._silero
                model_path = Path.home() / ".cache/torch/hub/snakers4_silero-models_master/src/silero/model/v4_ru.pt"
                if not model_path.is_file():
                    raise RuntimeError("Provision Silero v4_ru.pt locally before inference: " + str(model_path))
                import torch.package
                tts._silero = torch.package.PackageImporter(str(model_path)).load_pickle("tts_models", "model")
                return tts._silero
        def no_f5_download():
            raise RuntimeError("F5 auto-loading is disabled; use the local Silero model")
        tts._load_silero = load_silero
        tts._load_f5 = no_f5_download
        tts._https_local_only = True


def read_token(filename):
    if filename is None:
        return None
    token = Path(filename).read_text(encoding="ascii").strip()
    if not 24 <= len(token) <= 4096 or not re.fullmatch(r"[A-Za-z0-9._~+/=-]+", token):
        raise ValueError("Token file must contain a 24-4096 character ASCII bearer token")
    return token


def validate_bind(host, token):
    try:
        loopback = ipaddress.ip_address(host).is_loopback
    except ValueError:
        loopback = host.lower() == "localhost"
    if not loopback and token is None:
        raise ValueError("--token-file is required when binding outside loopback")


def tls_context(cert, key):
    if not Path(cert).is_file() or not Path(key).is_file():
        raise ValueError("Both TLS certificate and private key files are required")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(cert), str(key))
    return context


def create_app(backend, token=None, warmup=False, max_request_bytes=32 * 1024 * 1024):
    if not 1 <= max_request_bytes <= 32 * 1024 * 1024:
        raise ValueError("Request body limit must be 1 byte to 32 MiB")
    expected = ("Bearer " + token).encode("ascii") if token else None
    @web.middleware
    async def authenticate(request, handler):
        if expected is not None:
            provided = request.headers.get("Authorization", "").encode("utf-8")
            if not hmac.compare_digest(provided, expected):
                raise web.HTTPUnauthorized(headers={"WWW-Authenticate": "Bearer"})
        return await handler(request)
    app = web.Application(client_max_size=max_request_bytes, middlewares=[authenticate])
    for method, path, name in ROUTES:
        handler = getattr(backend, name, None)
        if not callable(handler):
            raise ValueError("Backend is missing handler: " + name)
        app.router.add_route(method, path, handler)
    if warmup:
        callback = getattr(backend, "on_startup", None)
        if not callable(callback):
            raise ValueError("Backend does not provide an explicit warmup callback")
        app.on_startup.append(callback)
    return app


def parser():
    result = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    result.add_argument("--backend-dir", required=True, type=Path)
    result.add_argument("--cert", required=True, type=Path)
    result.add_argument("--key", required=True, type=Path)
    result.add_argument("--host", default="127.0.0.1")
    result.add_argument("--port", type=int, default=8443)
    result.add_argument("--token-file", type=Path)
    result.add_argument("--warmup", action="store_true")
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    if not 1 <= args.port <= 65535:
        raise ValueError("Port must be between 1 and 65535")
    token = read_token(args.token_file)
    validate_bind(args.host, token)
    context = tls_context(args.cert, args.key)
    # These additionally keep HF-based optional backend dependencies offline.
    os.environ["HF_HUB_OFFLINE"] = "1"
    os.environ["TRANSFORMERS_OFFLINE"] = "1"
    backend = load_backend(args.backend_dir)
    local_only_models(backend)
    app = create_app(backend, token=token, warmup=args.warmup)
    logging.basicConfig(level=logging.INFO)
    web.run_app(app, host=args.host, port=args.port, ssl_context=context,
                access_log=None, print=None)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError) as exc:
        raise SystemExit(str(exc)) from None
