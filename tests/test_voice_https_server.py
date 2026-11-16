"""TLS voice launcher tests: fake backend only, no bound sockets or AI weights."""
import asyncio
import importlib.util
import json
from pathlib import Path
import ssl
import subprocess
import sys
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest
from aiohttp import web
from aiohttp.test_utils import make_mocked_request

SOURCE = Path(__file__).resolve().parents[1] / "tools/run_voice_https.py"
spec = importlib.util.spec_from_file_location("voice_https_launcher_test", SOURCE)
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)
TOKEN = "regression-token-0123456789abcdef"


@pytest.fixture
def backend(tmp_path):
    package = tmp_path / "backend/server"
    package.mkdir(parents=True)
    (package / "__init__.py").write_text("")
    (package / "config.py").write_text("MARKER = 'private-namespace'\n")
    (package / "app.py").write_text("""
from aiohttp import web
from .config import MARKER
calls = []
async def health(request):
    calls.append('health')
    return web.json_response({'status': 'ok', 'marker': MARKER})
async def asr_endpoint(request):
    body = await request.read()
    calls.append('asr')
    return web.json_response({'text': 'recognized', 'bytes': len(body)})
async def chat(request):
    calls.append('chat')
    return web.json_response({'text': 'reply', 'emotion': 'neutral'})
async def tts_endpoint(request):
    calls.append('tts')
    return web.Response(body=b'RIFFfake-WAVE', content_type='audio/wav')
async def on_startup(app):
    calls.append('warmup')
""")
    return launcher.load_backend(package.parent)


def dispatch(app, method, path, headers=None, body=b""):
    async def run():
        app.freeze()
        await app.startup()
        try:
            payload = Mock()
            payload.readany = AsyncMock(side_effect=[body, b""])
            request = make_mocked_request(method, path, headers=headers or {},
                                          app=app, payload=payload,
                                          client_max_size=app._client_max_size)
            try:
                return await app._handle(request)
            except web.HTTPException as response:
                return response
        finally:
            await app.cleanup()
    return asyncio.run(run())


def test_only_four_explicit_http_routes_no_websocket_or_implicit_head(backend):
    app = launcher.create_app(backend, TOKEN)
    routes = {(route.method, route.resource.canonical) for route in app.router.routes()}
    assert routes == {('POST', '/asr'), ('POST', '/chat'), ('POST', '/tts'), ('GET', '/health')}
    assert 'warmup' not in backend.calls


@pytest.mark.parametrize('method,path', [('GET', '/health'), ('POST', '/asr'), ('POST', '/chat'), ('POST', '/tts')])
def test_authenticated_request_reaches_actual_registered_backend_handler(backend, method, path):
    response = dispatch(launcher.create_app(backend, TOKEN), method, path,
                        {'Authorization': 'Bearer ' + TOKEN}, b'audio')
    assert response.status == 200
    assert path[1:] in backend.calls
    assert 'warmup' not in backend.calls
    if path == '/asr':
        assert json.loads(response.text)['bytes'] == 5
    if path == '/tts':
        assert response.content_type == 'audio/wav'


@pytest.mark.parametrize('provided', ['', 'Bearer wrong', 'Basic credentials', 'Bearer nonascii-\u044f'])
def test_bad_or_missing_bearer_rejected_before_backend(backend, provided):
    response = dispatch(launcher.create_app(backend, TOKEN), 'POST', '/chat', {'Authorization': provided})
    assert response.status == 401
    assert response.headers['WWW-Authenticate'] == 'Bearer'
    assert not backend.calls


@pytest.mark.parametrize('method,path,status', [('GET', '/voice', 404), ('GET', '/asr', 405), ('HEAD', '/health', 405), ('POST', '/health', 405)])
def test_websocket_and_unexpected_methods_are_unavailable(backend, method, path, status):
    response = dispatch(launcher.create_app(backend, TOKEN), method, path,
                        {'Authorization': 'Bearer ' + TOKEN, 'Upgrade': 'websocket'})
    assert response.status == status
    assert not backend.calls


def test_warmup_requires_explicit_opt_in(backend):
    response = dispatch(launcher.create_app(backend, TOKEN, warmup=True), 'GET', '/health',
                        {'Authorization': 'Bearer ' + TOKEN})
    assert response.status == 200
    assert backend.calls == ['warmup', 'health']


def test_request_size_limit_applies_before_asr_processing(backend):
    response = dispatch(launcher.create_app(backend, TOKEN, max_request_bytes=4), 'POST', '/asr',
                        {'Authorization': 'Bearer ' + TOKEN}, b'too-large')
    assert response.status == 413
    assert not backend.calls


def test_load_backend_has_private_package_namespace(tmp_path, monkeypatch):
    fake_server = SimpleNamespace(marker='unrelated-installed-server')
    monkeypatch.setitem(sys.modules, 'server', fake_server)
    package = tmp_path / 'another/server'
    package.mkdir(parents=True)
    (package / 'app.py').write_text('MARKER = 123\n')
    module = launcher.load_backend(package.parent)
    assert module.MARKER == 123
    assert sys.modules['server'] is fake_server
    assert module.__package__.startswith('_atri_voice_backend_')


@pytest.mark.parametrize('host', ['0.0.0.0', '192.168.1.10', '::', 'voice.example'])
def test_nonloopback_bind_requires_token(host):
    with pytest.raises(ValueError, match='token-file'):
        launcher.validate_bind(host, None)
    launcher.validate_bind(host, TOKEN)


@pytest.mark.parametrize('host', ['127.0.0.1', '::1', 'localhost'])
def test_loopback_only_can_explicitly_omit_token(host):
    launcher.validate_bind(host, None)


def test_token_file_validation_and_no_secret_output(tmp_path):
    token = tmp_path / 'token'
    token.write_text(TOKEN + '\n')
    assert launcher.read_token(token) == TOKEN
    token.write_text('short')
    with pytest.raises(ValueError) as exc:
        launcher.read_token(token)
    assert 'short' not in str(exc.value)


def test_model_cache_guards_do_not_import_or_download_models(monkeypatch, tmp_path):
    models = []
    def original(name):
        import gigaam
        return gigaam._download_file('https://must-not-download.invalid/weights', str(tmp_path / 'missing.ckpt'))
    asr = SimpleNamespace(load=original)
    tts = SimpleNamespace(_silero=None)
    module = SimpleNamespace(asr=asr, tts=tts)
    gigaam = SimpleNamespace(_download_file=lambda *args: models.append(args))
    monkeypatch.setitem(sys.modules, 'gigaam', gigaam)
    monkeypatch.setattr(launcher.Path, 'home', lambda: tmp_path)
    launcher.local_only_models(module)
    assert not models
    with pytest.raises(RuntimeError, match='locally'):
        asr.load('ctc')
    with pytest.raises(RuntimeError, match='locally'):
        tts._load_silero()
    with pytest.raises(RuntimeError, match='disabled'):
        tts._load_f5()
    assert not models
    cached = tmp_path / 'missing.ckpt'
    cached.write_bytes(b'preprovisioned')
    assert asr.load('ctc') == str(cached)


def test_tls_cert_and_key_are_mandatory(tmp_path):
    with pytest.raises(ValueError, match='required'):
        launcher.tls_context(tmp_path / 'missing.pem', tmp_path / 'missing.key')
    with pytest.raises(SystemExit):
        launcher.parser().parse_args(['--backend-dir', str(tmp_path)])


def test_real_tls_context_uses_certificate_and_tls12_minimum(tmp_path):
    cert = tmp_path / 'server.pem'
    key = tmp_path / 'server.key'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-days', '1', '-subj', '/CN=localhost', '-keyout', str(key), '-out', str(cert)],
                   check=True, capture_output=True)
    context = launcher.tls_context(cert, key)
    assert context.protocol == ssl.PROTOCOL_TLS_SERVER
    assert context.minimum_version >= ssl.TLSVersion.TLSv1_2


def test_main_passes_ssl_context_to_only_listener_without_starting_it(backend, tmp_path, monkeypatch):
    cert = tmp_path / 'cert'
    key = tmp_path / 'key'
    token = tmp_path / 'token'
    token.write_text(TOKEN)
    context = object()
    monkeypatch.setenv("HF_HUB_OFFLINE", "0")
    monkeypatch.setenv("TRANSFORMERS_OFFLINE", "0")
    calls = []
    monkeypatch.setattr(launcher, 'tls_context', lambda c, k: context)
    monkeypatch.setattr(launcher, 'load_backend', lambda d: backend)
    monkeypatch.setattr(launcher.web, 'run_app', lambda app, **kwargs: calls.append((app, kwargs)))
    assert launcher.main(['--backend-dir', str(tmp_path), '--cert', str(cert), '--key', str(key),
                          '--token-file', str(token)]) == 0
    assert len(calls) == 1
    _, kwargs = calls[0]
    assert kwargs['host'] == '127.0.0.1' and kwargs['port'] == 8443
    assert kwargs['ssl_context'] is context
    assert not backend.calls
