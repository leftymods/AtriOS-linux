"""Fresh-client provisioning and bearer-token storage, without network/audio."""
import json
import os
import stat
import sys

import pytest
from test_runtime_regressions import load_script


@pytest.fixture
def ai(tmp_path, monkeypatch):
    module = load_script("atriai", monkeypatch)
    monkeypatch.setattr(module, "CONFIG_FILE", str(tmp_path / "private" / "ai.json"))
    monkeypatch.setattr(module, "STATUS_FILE", str(tmp_path / "status.json"))
    return module


def test_fresh_client_has_no_personal_endpoint_and_is_disabled(ai):
    cfg = ai.load_config()
    assert cfg["server"] == ""
    assert cfg["enabled"] is False
    assert cfg["token"] == ""


def test_missing_endpoint_cannot_enable_or_persist_microphone_client(ai, monkeypatch):
    monkeypatch.setattr(sys, "argv", ["atriai", "enable"])
    with pytest.raises(ValueError, match="HTTPS"):
        ai.main()
    assert not os.path.exists(ai.CONFIG_FILE)


def test_saving_tokens_is_private_even_under_permissive_umask(ai, tmp_path):
    cfg = {"server": "https://voice.example", "token": "private-bearer", "enabled": False}
    previous = os.umask(0o022)
    try:
        ai.save_config(cfg)
        assert stat.S_IMODE(os.stat(ai.CONFIG_FILE).st_mode) == 0o600
        os.chmod(ai.CONFIG_FILE, 0o644)
        ai.save_config({**cfg, "wake_word": "Atri"})
        assert stat.S_IMODE(os.stat(ai.CONFIG_FILE).st_mode) == 0o600
    finally:
        os.umask(previous)
    assert ai.load_config()["token"] == cfg["token"]
    assert not list((tmp_path / "private").glob(".ai-config-*"))


def test_failed_save_preserves_previous_config_and_cleans_temporary(ai, tmp_path):
    cfg = {"server": "https://voice.example", "token": "private-bearer"}
    ai.save_config(cfg)
    before = open(ai.CONFIG_FILE, "rb").read()
    with pytest.raises(TypeError):
        ai.save_config({"cannot_encode": object()})
    assert open(ai.CONFIG_FILE, "rb").read() == before
    assert not list((tmp_path / "private").glob(".ai-config-*"))


def test_enabling_configured_server_preserves_credentials_and_status_redacts(ai, monkeypatch, capsys):
    ai.save_config({"server": "https://voice.example", "token": "private-bearer", "ca_file": "/public-ca.pem"})
    monkeypatch.setattr(sys, "argv", ["atriai", "enable"])
    ai.main()
    assert ai.load_config()["enabled"] is True
    assert ai.load_config()["token"] == "private-bearer"
    capsys.readouterr()
    monkeypatch.setattr(sys, "argv", ["atriai", "status"])
    ai.main()
    output = capsys.readouterr().out
    assert "private-bearer" not in output
    status = json.loads(output)
    assert status["config"]["token"] == "***"
    assert status["config"]["server"] == "https://voice.example"
    assert ai.load_config()["token"] == "private-bearer"
