"""Board selection regressions using fake make/FIP; never compile or flash."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture
def project(tmp_path):
    root = tmp_path / "project"
    for directory in ["scripts", "lib", "config/boards", "config/bootloaders", "config/families", "cache/uboot", "cache/fip", "mock-bin"]:
        (root / directory).mkdir(parents=True, exist_ok=True)
    for name in ["lib/general.sh", "lib/config.sh", "lib/release-board.sh", "scripts/build_mainline_uboot.sh"]:
        shutil.copyfile(ROOT / name, root / name)
    (root / "config/system.conf").write_text('OS_NAME="AtriOS"\nRELEASE_NAME="Nebula"\nRELEASE_VERSION="1.0"\n')
    (root / "config/families/test.conf").write_text('ARCH="arm64"\nUBOOT_ARCH="arm"\n')
    (root / "cache/uboot/Makefile").write_text("# fake source tree\n")
    for suffix in ("alpha", "beta"):
        board = "speaker-" + suffix
        source = "board/vendor/" + suffix
        conf = f"""BOARD="{board}"
BOARD_NAME="Speaker {suffix}"
BOARDFAMILY="test"
SOC="sm1"
ARCH="arm64"
DTS_NAME="meson-{suffix}"
UBOOT_SOURCE_DIR="{source}"
UPGRADE_DIR="device/vendor/{suffix}/upgrade"
FIP_SOC="fip-{suffix}"
UBOOT_DEFCONFIG="not-used-upstream-name"
KERNEL_DEFCONFIG="test_defconfig"
SERIAL_CONSOLE="ttyS0,115200"
ROOTFS_OVERLAY="{source}/overlay"
"""
        (root / f"config/boards/{board}.conf").write_text(conf)
        directory = root / source
        (directory / "dts").mkdir(parents=True)
        (directory / "overlay/etc").mkdir(parents=True)
        (directory / "u-boot.config").write_text("CONFIG_TEST=y\nCONFIG_BOARD_" + suffix.upper() + "=y\n")
        (directory / f"dts/meson-{suffix}.dts").write_text("// " + suffix + " device tree\n")
        (root / f"config/bootloaders/meson-{suffix}.conf").write_text("""UBOOT_CONFIG_SOURCE="$UBOOT_SOURCE_DIR/u-boot.config"
UBOOT_DTS_SOURCE="$UBOOT_SOURCE_DIR/dts/$DTS_NAME.dts"
UBOOT_DTS_TARGET="arch/arm/dts/$DTS_NAME.dts"
UBOOT_REQUIRED_OPTIONS="CONFIG_TEST"
FIP_PROFILE="$FIP_SOC"
""")
        (root / f"cache/fip/fip-{suffix}").mkdir()
    make = root / "mock-bin/make"
    make.write_text("""#!/usr/bin/env python3
from pathlib import Path
import shutil, sys
with Path('make.calls').open('a') as log: log.write(' '.join(sys.argv[1:]) + chr(10))
if sys.argv[1].endswith('_defconfig'):
    shutil.copyfile(Path('configs') / sys.argv[1], '.config')
else:
    Path('u-boot.bin').write_bytes(Path('.config').read_bytes())
""")
    make.chmod(0o755)
    git = root / "mock-bin/git"
    git.write_text('#!/bin/sh\necho "Unexpected network/git invocation" >&2\nexit 97\n')
    git.chmod(0o755)
    (root / "cache/fip/build-fip.sh").write_text("""#!/bin/bash
set -eu
mkdir -p "$3"
cp "$2" "$3/u-boot.bin"
for name in u-boot.bin.sd.bin u-boot.bin.usb.bl2 u-boot.bin.usb.tpl; do
  printf '%s' "$1" > "$3/$name"
done
""")
    return root


def build(root, board="speaker-alpha"):
    env = {key: value for key, value in os.environ.items() if key not in (
        "BOOTLOADER_BIN", "DTB_IMAGE", "KERNEL_IMAGE", "ROOTFS_IMAGE", "UBOOT_SRC", "FIP_TOOL_DIR",
        "BOOTLOADER_PROFILE", "FIP_PROFILE", "UBOOT_CONFIG_SOURCE", "UBOOT_DTS_SOURCE", "UPGRADE_DIR")}
    env.update(BOARD=board, BUILD_JOBS="1", PATH=str(root / "mock-bin") + os.pathsep + env.get("PATH", ""))
    return subprocess.run(["bash", str(root / "scripts/build_mainline_uboot.sh")], cwd=root, env=env, text=True, capture_output=True, timeout=20)


def test_each_board_selects_its_own_config_dts_fip_and_output(project):
    for suffix in ("alpha", "beta"):
        result = build(project, "speaker-" + suffix)
        assert result.returncode == 0, result.stdout + result.stderr
        output = project / f"device/vendor/{suffix}/upgrade"
        assert (output / "u-boot.bin.sd.bin").read_text() == "fip-" + suffix
        assert "CONFIG_BOARD_" + suffix.upper() + "=y" in (output / "bootloader.img").read_text()
        assert (project / f"cache/uboot/arch/arm/dts/meson-{suffix}.dts").read_text() == "// " + suffix + " device tree\n"
    assert not (project / "device/yandex").exists()


@pytest.mark.parametrize("failure", ["missing-profile", "missing-dts", "missing-fip", "escaping-target", "escaping-output", "wrong-board-id"])
def test_unreviewed_or_invalid_board_fails_before_build(project, failure):
    profile = project / "config/bootloaders/meson-alpha.conf"
    board = project / "config/boards/speaker-alpha.conf"
    if failure == "missing-profile":
        profile.unlink()
    elif failure == "missing-dts":
        (project / "board/vendor/alpha/dts/meson-alpha.dts").unlink()
    elif failure == "missing-fip":
        with profile.open("a") as out: out.write('FIP_PROFILE="not-reviewed"\n')
    elif failure == "escaping-target":
        with profile.open("a") as out: out.write('UBOOT_DTS_TARGET="../escape.dts"\n')
    elif failure == "escaping-output":
        with board.open("a") as out: out.write('UPGRADE_DIR="../outside"\n')
    else:
        with board.open("a") as out: out.write('BOARD="speaker-beta"\n')
    result = build(project)
    assert result.returncode != 0
    assert not (project / "cache/uboot/make.calls").exists()
    assert not (project / "device").exists()


def test_staging_symlink_cannot_redirect_writes(project, tmp_path):
    outside = tmp_path / "outside"
    outside.write_text("preserve")
    destination = project / "cache/uboot/arch/arm/dts/meson-alpha.dts"
    destination.parent.mkdir(parents=True)
    destination.symlink_to(outside)
    result = build(project)
    assert result.returncode != 0
    assert outside.read_text() == "preserve"
    assert not (project / "cache/uboot/make.calls").exists()


def test_release_metadata_uses_selected_board_overlay_dtb_and_image(project):
    env = {key: value for key, value in os.environ.items() if key not in ("DTB_IMAGE", "BOOTLOADER_BIN", "ROOTFS_IMAGE", "KERNEL_IMAGE")}
    env["PROJECT_ROOT"] = str(project)
    code = 'source "$PROJECT_ROOT/lib/release-board.sh"; release_board_config speaker-beta >/dev/null; printf "%s\n" "$ROOTFS_OVERLAY_PATH" "$DTB_IMAGE" "$IMAGE_FILENAME"'
    result = subprocess.run(["bash", "-ec", code], env=env, text=True, capture_output=True, check=True)
    assert result.stdout.splitlines() == [str(project / "board/vendor/beta/overlay"), str(project / "output/images/meson-beta.dtb"), "AtriOS-Nebula-speaker-beta-emmc.img"]


def test_current_station_aliases_use_same_reviewed_recipe_without_rewriting_profiles():
    for board in ("station-max", "yndxsttion53"):
        conf = (ROOT / f"config/boards/{board}.conf").read_text()
        assert 'DTS_NAME="meson-sm1-yandex-station-max"' in conf
    profile = (ROOT / "config/bootloaders/meson-sm1-yandex-station-max.conf").read_text()
    assert 'FIP_PROFILE="sei610"' in profile
    assert 'uboot_station_max_defconfig' in profile


def test_release_workflow_consumes_resolved_paths_and_does_not_choose_station_dtb():
    text = (ROOT / ".github/workflows/release-mainline.yml").read_text()
    assert 'source lib/release-board.sh' in text
    assert '--dtb "$DTB_IMAGE"' in text
    assert '"$ROOTFS_OVERLAY_PATH/etc/atrios/ota-public.pem"' in text
    assert '"output/images/$IMAGE_FILENAME"' in text
    assert 'meson-sm1-yandex-station-max.dtb' not in text
    assert 'board/yandex/station2/rootfs_overlay' not in text
