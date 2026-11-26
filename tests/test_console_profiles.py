"""Exercise per-board console configuration without real TTYs or a rootfs build."""
import json
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "lib/runtime/console.sh"


@pytest.fixture
def console_stage(tmp_path):
    stage = tmp_path / "stage"
    (stage / "etc/systemd/system").mkdir(parents=True)
    securetty = stage / "etc/securetty"
    securetty.write_text("existing-console\n")
    securetty.chmod(0o640)
    commands = tmp_path / "commands"
    commands.mkdir()
    (commands / "sudo").write_text('#!/bin/sh\nexec "$@"\n')
    (commands / "chroot").write_text("#!/usr/bin/env python3\nimport json, os, sys\nwith open(os.environ['CHROOT_LOG'], 'a') as f: f.write(json.dumps(sys.argv[1:]) + chr(10))\n")
    for command in commands.iterdir():
        command.chmod(0o755)
    return stage, {**os.environ, "PROJECT_ROOT": str(ROOT), "STAGE": str(stage),
                   "CHROOT_LOG": str(tmp_path / "chroot.log"),
                   "PATH": str(commands) + os.pathsep + os.environ["PATH"]}


def configure(stage, env, console):
    return subprocess.run(["bash", "-ec", 'source "$PROJECT_ROOT/lib/general.sh"; source "$PROJECT_ROOT/lib/rootfs-debian.sh"; configure_rootfs_console "$STAGE"'],
                          env={**env, "SERIAL_CONSOLE": console}, text=True, capture_output=True)


@pytest.mark.parametrize("console,tty", [("ttyS0,9600n8", "ttyS0"), ("ttyAMA0,115200", "ttyAMA0"),
                                          ("ttyAML0,115200", "ttyAML0"), ("hvc0", "hvc0")])
def test_board_console_is_used_for_data_securetty_and_systemd(console_stage, console, tty):
    stage, env = console_stage
    result = configure(stage, env, console)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (stage / "etc/atrios-console").read_text() == console + "\n"
    assert (stage / "etc/atrios-console").stat().st_mode & 0o777 == 0o644
    assert (stage / "usr/lib/atrios/console.sh").read_bytes() == HELPER.read_bytes()
    assert (stage / "etc/securetty").read_text().splitlines() == ["existing-console", tty, "console", "tty0", "tty1"]
    assert (stage / "etc/securetty").stat().st_mode & 0o777 == 0o640
    calls = [json.loads(line) for line in Path(env["CHROOT_LOG"]).read_text().splitlines()]
    assert calls[0][-1] == "serial-getty@" + tty + ".service"
    assert configure(stage, env, console).returncode == 0
    assert (stage / "etc/securetty").read_text().splitlines().count(tty) == 1


@pytest.mark.parametrize("console", ["", "/dev/ttyS0", "../ttyS0", "-ttyS0", "ttyS0,", "ttyS0,0", "ttyS0,115200,8", "ttyS0,115200\ntouch marker", "ttyS0,$(touch marker)", "ttyS0,115200n9", "ttyS0 space"])
def test_invalid_console_fails_before_any_configuration_write(console_stage, console):
    stage, env = console_stage
    result = configure(stage, env, console)
    assert result.returncode != 0
    assert "Invalid SERIAL_CONSOLE" in result.stdout
    assert not (stage / "etc/atrios-console").exists()
    assert not Path(env["CHROOT_LOG"]).exists()
    assert (stage / "etc/securetty").read_text() == "existing-console\n"


def test_console_configuration_does_not_follow_output_symlinks(console_stage, tmp_path):
    stage, env = console_stage
    outside = tmp_path / "outside"
    outside.write_text("keep")
    (stage / "etc/atrios-console").symlink_to(outside)
    result = configure(stage, env, "ttyS0,115200")
    assert result.returncode != 0
    assert outside.read_text() == "keep"
    assert not Path(env["CHROOT_LOG"]).exists()


@pytest.mark.parametrize("spec,present,expected", [("ttyAMA0,115200n8", True, "ttyAMA0"),
                                                    ("ttyS0,9600", True, "ttyS0"),
                                                    ("ttyAMA0,115200", False, "console"),
                                                    ("../outside,115200", True, "console"),
                                                    ("ttyS0,115200\ntouch marker", True, "console")])
def test_init_uses_selected_character_device_or_kernel_console(tmp_path, spec, present, expected):
    devices = tmp_path / "dev"
    devices.mkdir()
    # A harmless character device provides a TTY-shaped fixture; never open it.
    for name in ("ttyAMA0", "ttyS0"):
        if present:
            (devices / name).symlink_to("/dev/null")
    config = tmp_path / "console-data"
    config.write_text(spec + "\n")
    result = subprocess.run(["sh", "-ec", '. "$HELPER"; atrios_console_path'],
                            env={**os.environ, "HELPER": str(HELPER), "ATRIOS_CONSOLE_CONFIG_FILE": str(config), "ATRIOS_CONSOLE_DEV_DIR": str(devices)},
                            capture_output=True, text=True)
    assert result.returncode == 0
    assert result.stdout.strip() == str(devices / expected)
    assert not (tmp_path / "marker").exists()


def test_missing_console_data_uses_kernel_console(tmp_path):
    result = subprocess.run(["sh", "-ec", '. "$HELPER"; atrios_console_path'],
                            env={**os.environ, "HELPER": str(HELPER), "ATRIOS_CONSOLE_CONFIG_FILE": str(tmp_path / "missing")}, capture_output=True, text=True)
    assert result.returncode == 0
    assert result.stdout.strip() == "/dev/console"


def test_family_preserves_board_console_and_supplies_default_only_when_absent():
    family = ROOT / "config/families/amlogic-meson64.conf"
    for specified, expected in [("ttyAMA0,9600n8", "ttyAMA0,9600n8"), ("", "ttyAML0,115200")]:
        result = subprocess.run(["bash", "-ec", 'source "$FAMILY"; printf "%s\\n" "$SERIAL_CONSOLE"'],
                                env={**os.environ, "FAMILY": str(family), "SERIAL_CONSOLE": specified}, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert result.stdout.strip() == expected


def test_generated_init_uses_common_console_selection_and_private_tty_mode():
    recipe = (ROOT / "lib/rootfs-debian.sh").read_text()
    init = recipe.split("cat <<'INITEOF'", 1)[1].split("\nINITEOF", 1)[0]
    assert "configure_rootfs_console \"$stage_dir\"" in recipe
    assert '. /usr/lib/atrios/console.sh' in init
    assert "CONSOLE_PATH=$(atrios_console_path)" in init
    assert 'chmod 600 "$CONSOLE_PATH"' in init
    assert "ttyAML0" not in init
    assert '"/dev/$CONSOLE_DEV"' not in init
    assert subprocess.run(["sh", "-n", str(HELPER)], capture_output=True).returncode == 0
    assert subprocess.run(["bash", "-n", str(ROOT / "lib/rootfs-debian.sh")], capture_output=True).returncode == 0
