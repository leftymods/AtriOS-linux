"""Offline staging text and hostname bootstrap regression tests; no root/hardware."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("rootfs_text_under_test", ROOT / "lib/normalize-rootfs-text.py")
NORMALIZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NORMALIZER)
HOSTNAME = ROOT / "lib/runtime/S00hostname"


def write(root, name, data):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def test_packaging_normalizes_fish_but_preserves_binary_and_settings_data(tmp_path):
    stage = tmp_path / "stage"
    vendor = write(stage, "usr/share/fish/functions/fish_command_not_found.fish", b"function fish_command_not_found\r\n echo no\r\nend\r\n")
    user = write(stage, "root/.config/fish/config.fish", bytes([92, 13, 10]) + b"if status is-interactive\r\n    set -g MY_CUSTOM_SETTING preserved\r\nend\n")
    script = write(stage, "usr/bin/atrios", b"#!/bin/sh\r\necho custom\r\n")
    binary = write(stage, "usr/bin/fish", b"\x7fELF\x00\r\n\x80")
    firmware = write(stage, "lib/firmware/blob.bin", b"\x00\r\n\xff")
    image = write(stage, "usr/share/fish/tools/icon.png", b"\x89PNG\r\n\x1a\n\x00")
    variables = write(stage, "root/.config/fish/fish_variables", b"# VARIABLE DATA\r\nSET custom:value\r\n")
    untouched = {p: p.read_bytes() for p in (binary, firmware, image, variables)}
    vendor.chmod(0o640)
    binary.chmod(0o751)
    commands = tmp_path / "commands"
    commands.mkdir()
    sudo = commands / "sudo"
    sudo.write_text('#!/bin/sh\nexec "$@"\n')
    sudo.chmod(0o755)
    env = {**os.environ, "PROJECT_ROOT": str(ROOT), "STAGE": str(stage), "PATH": str(commands) + os.pathsep + os.environ["PATH"]}
    result = subprocess.run(["bash", "-ec", 'source "$PROJECT_ROOT/lib/rootfs-debian.sh"; normalize_rootfs_startup_text "$STAGE"'], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert vendor.read_bytes() == b"function fish_command_not_found\n echo no\nend\n"
    assert user.read_bytes() == b"if status is-interactive\n    set -g MY_CUSTOM_SETTING preserved\nend\n"
    assert script.read_bytes() == b"#!/bin/sh\necho custom\n"
    assert vendor.stat().st_mode & 0o777 == 0o640
    assert binary.stat().st_mode & 0o777 == 0o751
    assert {p: p.read_bytes() for p in untouched} == untouched
    assert NORMALIZER.normalize(stage) == []


def test_normalizer_preserves_real_fish_escapes(tmp_path):
    stage = tmp_path / "stage"
    expected = b"echo \\literal\nprintf 'a\\rb'\necho one \\\n two\n"
    path = write(stage, "etc/fish/config.fish", expected.replace(b"\n", b"\r\n"))
    NORMALIZER.normalize(stage)
    assert path.read_bytes() == expected


def test_normalizer_never_follows_directory_or_file_symlinks(tmp_path):
    stage = tmp_path / "stage"
    fish = stage / "usr/share/fish"
    fish.mkdir(parents=True)
    outside = tmp_path / "external"
    outside.mkdir()
    original = write(outside, "outside.fish", b"echo keep\r\n")
    (fish / "linked.fish").symlink_to(original)
    (fish / "nested").symlink_to(outside, target_is_directory=True)
    assert NORMALIZER.normalize(stage) == []
    assert original.read_bytes() == b"echo keep\r\n"


def test_binary_masquerading_as_fish_is_never_rewritten(tmp_path):
    stage = tmp_path / "stage"
    path = write(stage, "etc/fish/binary.fish", b"\x7fELF\x00\r\n")
    with pytest.raises(ValueError, match="binary"):
        NORMALIZER.normalize(stage)
    assert path.read_bytes() == b"\x7fELF\x00\r\n"


def test_running_root_is_never_an_offline_packaging_target():
    with pytest.raises(ValueError, match="offline"):
        NORMALIZER.normalize(Path("/"))


@pytest.mark.parametrize("value", ["AtriOS\n", "speaker-2\r\n", "speaker.home\n", "x" * 63])
def test_bootstrap_applies_valid_hostname_without_touching_host(tmp_path, value):
    config = tmp_path / "hostname"
    config.write_bytes(value.encode())
    binaries = tmp_path / "bin"
    binaries.mkdir()
    capture = tmp_path / "captured"
    command = binaries / "hostname"
    command.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$CAPTURE"\n')
    command.chmod(0o755)
    result = subprocess.run(["sh", str(HOSTNAME), "start"], env={**os.environ, "HOSTNAME_FILE": str(config), "CAPTURE": str(capture), "PATH": str(binaries) + os.pathsep + os.environ["PATH"]}, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert capture.read_text() == value.strip() + "\n"


@pytest.mark.parametrize("value", ["", "-option", "good.-bad", "good.bad-", "bad name", "bad;command", "bad\nsecond", ".bad", "bad.", "bad..name", "x" * 64])
def test_bootstrap_rejects_invalid_hostname_without_command_execution(tmp_path, value):
    config = tmp_path / "hostname"
    config.write_text(value)
    binaries = tmp_path / "bin"
    binaries.mkdir()
    capture = tmp_path / "captured"
    command = binaries / "hostname"
    command.write_text('#!/bin/sh\necho called > "$CAPTURE"\n')
    command.chmod(0o755)
    result = subprocess.run(["sh", str(HOSTNAME), "start"], env={**os.environ, "HOSTNAME_FILE": str(config), "CAPTURE": str(capture), "PATH": str(binaries) + os.pathsep + os.environ["PATH"]}, capture_output=True, text=True)
    assert result.returncode == 0
    assert not capture.exists()
    assert "invalid" in result.stderr


def test_generated_init_applies_common_hostname_before_service_startup():
    source = (ROOT / "lib/rootfs-debian.sh").read_text()
    generated = source.split("cat <<'INITEOF'", 1)[1].split("\nINITEOF", 1)[0]
    assert generated.index("mount -a") < generated.index("/bin/sh /etc/init.d/S00hostname start") < generated.index("/lib/systemd/systemd")
    assert 'install -D -m 755 "$PROJECT_ROOT/lib/runtime/S00hostname"' in source
    assert 'normalize_rootfs_startup_text "$stage_dir"' in source
    assert "dos2unix" not in source
    assert HOSTNAME.read_bytes() == (ROOT / "board/yandex/station2/rootfs_overlay/etc/init.d/S00hostname").read_bytes()
    assert b"\r\n" not in (ROOT / "board/yandex/station2/rootfs_overlay/usr/share/fish/functions/fish_command_not_found.fish").read_bytes()
