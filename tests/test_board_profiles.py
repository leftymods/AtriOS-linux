"""Board selection regressions using fake make/FIP; never compile or flash."""
import os
import json
import hashlib
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def initialize_git(directory, message):
    subprocess.run(['/usr/bin/git', 'init', '-q', str(directory)], check=True)
    subprocess.run(['/usr/bin/git', '-C', str(directory), 'add', '.'], check=True)
    subprocess.run(['/usr/bin/git', '-C', str(directory), '-c', 'user.name=Board Test',
                    '-c', 'user.email=board-test@example.invalid', 'commit', '-qm', message], check=True)
    return subprocess.check_output(['/usr/bin/git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()


@pytest.fixture
def project(tmp_path):
    root = tmp_path / "project"
    for directory in ["scripts", "lib", "config/boards", "config/bootloaders", "config/families", "mock-bin"]:
        (root / directory).mkdir(parents=True, exist_ok=True)
    for name in ["lib/general.sh", "lib/config.sh", "lib/release-board.sh", "scripts/build_mainline_uboot.sh"]:
        shutil.copyfile(ROOT / name, root / name)
    (root / "config/system.conf").write_text('OS_NAME="AtriOS"\nRELEASE_NAME="Nebula"\nRELEASE_VERSION="1.0"\n')
    (root / "config/families/test.conf").write_text('ARCH="arm64"\nUBOOT_ARCH="arm"\n')
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
        uboot = root / f'cache/uboot-{suffix}'
        uboot.mkdir(parents=True)
        (uboot / 'Makefile').write_text('# fake source tree ' + suffix + '\n')
        ub_commit = initialize_git(uboot, 'U-Boot ' + suffix)
        fip = root / f'cache/fip-{suffix}'
        (fip / ('fip-' + suffix)).mkdir(parents=True)
        (fip / ('fip-' + suffix) / 'board-id').write_text(suffix)
        (fip / 'build-fip.sh').write_text("""#!/bin/bash
set -eu
mkdir -p "$3"
cp "$2" "$3/u-boot.bin"
for name in u-boot.bin.sd.bin u-boot.bin.usb.bl2 u-boot.bin.usb.tpl; do
  printf '%s' "$1" > "$3/$name"
done
""")
        fip_commit = initialize_git(fip, 'FIP ' + suffix)
        (root / f"config/bootloaders/meson-{suffix}.conf").write_text(f"""UBOOT_CONFIG_SOURCE="$UBOOT_SOURCE_DIR/u-boot.config"
UBOOT_DTS_SOURCE="$UBOOT_SOURCE_DIR/dts/$DTS_NAME.dts"
UBOOT_DTS_TARGET="arch/arm/dts/$DTS_NAME.dts"
UBOOT_REQUIRED_OPTIONS="CONFIG_TEST"
FIP_PROFILE="$FIP_SOC"
UBOOT_GIT_URL="{uboot.as_uri()}"
UBOOT_GIT_COMMIT="{ub_commit}"
FIP_GIT_URL="{fip.as_uri()}"
FIP_GIT_COMMIT="{fip_commit}"
""")
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
    git.write_text('#!/bin/sh\ncase "$*" in *https:*|*http:*|*ssh:*) echo "Unexpected external network invocation" >&2; exit 97 ;; esac\nexec /usr/bin/git "$@"\n')
    git.chmod(0o755)
    return root


def build(root, board="speaker-alpha", *, explicit_cache=True):
    env = {key: value for key, value in os.environ.items() if key not in (
        "BOOTLOADER_BIN", "DTB_IMAGE", "KERNEL_IMAGE", "ROOTFS_IMAGE", "UBOOT_SRC", "FIP_TOOL_DIR",
        "BOOTLOADER_PROFILE", "FIP_PROFILE", "UBOOT_CONFIG_SOURCE", "UBOOT_DTS_SOURCE", "UPGRADE_DIR",
        "UBOOT_GIT_COMMIT", "FIP_GIT_COMMIT", "UBOOT_GIT_URL", "FIP_GIT_URL", "SOURCE_DATE_EPOCH")}
    suffix = board.removeprefix('speaker-')
    env.update(BOARD=board, BUILD_JOBS="1", UBOOT_SRC=str(root / f'cache/uboot-{suffix}'),
               FIP_TOOL_DIR=str(root / f'cache/fip-{suffix}'), PATH=str(root / "mock-bin") + os.pathsep + env.get("PATH", ""))
    if not explicit_cache:
        env.pop('UBOOT_SRC', None)
        env.pop('FIP_TOOL_DIR', None)
    return subprocess.run(["bash", str(root / "scripts/build_mainline_uboot.sh")], cwd=root, env=env, text=True, capture_output=True, timeout=20)


def test_each_board_selects_its_own_config_dts_fip_and_output(project):
    for suffix in ("alpha", "beta"):
        result = build(project, "speaker-" + suffix)
        assert result.returncode == 0, result.stdout + result.stderr
        output = project / f"device/vendor/{suffix}/upgrade"
        assert (output / "u-boot.bin.sd.bin").read_text() == "fip-" + suffix
        assert "CONFIG_BOARD_" + suffix.upper() + "=y" in (output / "bootloader.img").read_text()
        assert (project / f"cache/uboot-{suffix}/arch/arm/dts/meson-{suffix}.dts").read_text() == "// " + suffix + " device tree\n"
    assert not (project / "device/yandex").exists()
    provenance = [json.loads((project / f'device/vendor/{suffix}/upgrade/bootloader-sources.json').read_text()) for suffix in ('alpha', 'beta')]
    assert len({item['uboot']['commit'] for item in provenance}) == 2
    assert len({item['fip']['commit'] for item in provenance}) == 2


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
    assert not (project / "cache/uboot-alpha/make.calls").exists()
    assert not (project / "device").exists()


def test_staging_symlink_cannot_redirect_writes(project, tmp_path):
    outside = tmp_path / "outside"
    outside.write_text("preserve")
    destination = project / "cache/uboot-alpha/arch/arm/dts/meson-alpha.dts"
    destination.parent.mkdir(parents=True)
    destination.symlink_to(outside)
    result = build(project)
    assert result.returncode != 0
    assert outside.read_text() == "preserve"
    assert not (project / "cache/uboot-alpha/make.calls").exists()


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


@pytest.mark.parametrize('name', ['UBOOT_GIT_COMMIT', 'FIP_GIT_COMMIT'])
@pytest.mark.parametrize('value', ['', 'master', 'v2026.10', 'abc123'])
def test_branches_tags_and_short_hashes_are_not_source_pins(project, name, value):
    profile = project / 'config/bootloaders/meson-alpha.conf'
    with profile.open('a') as stream:
        stream.write(f'{name}="{value}"\nUBOOT_GIT_REF="master"\n')
    result = build(project)
    assert result.returncode != 0
    assert 'requires immutable' in result.stdout
    assert not (project / 'cache/uboot-alpha/make.calls').exists()
    assert not (project / 'device').exists()


@pytest.mark.parametrize('kind', ['uboot', 'fip'])
def test_wrong_existing_head_preserves_user_files_and_fails_before_staging(project, kind):
    directory = project / f'cache/{kind}-alpha'
    marker = directory / 'user-change.txt'
    marker.write_text('committed after pin')
    initialize_git(directory, 'Different unapproved HEAD')
    marker.write_text('keep my uncommitted user changes')
    before = subprocess.check_output(['/usr/bin/git', '-C', str(directory), 'diff', '--binary', 'HEAD'])
    result = build(project)
    assert result.returncode != 0
    assert 'pin mismatch' in result.stdout
    assert marker.read_text() == 'keep my uncommitted user changes'
    assert subprocess.check_output(['/usr/bin/git', '-C', str(directory), 'diff', '--binary', 'HEAD']) == before
    assert not (project / 'cache/uboot-alpha/make.calls').exists()
    assert not (project / 'device').exists()


def test_pinned_local_fetch_uses_exact_commits_even_when_remote_head_has_advanced(project):
    profile = project / 'config/bootloaders/meson-alpha.conf'
    text = profile.read_text()
    expected = {}
    for kind in ['uboot', 'fip']:
        directory = project / f'cache/{kind}-alpha'
        expected[kind] = subprocess.check_output(['/usr/bin/git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()
        source = project / f'remote/{kind}'
        source.parent.mkdir(parents=True, exist_ok=True)
        old_url = directory.as_uri()
        directory.rename(source)
        (source / 'remote-advanced').write_text('Do not select this moving HEAD')
        initialize_git(source, 'Remote advanced')
        text = text.replace(old_url, source.as_uri())
    profile.write_text(text)
    result = build(project)
    assert result.returncode == 0, result.stdout + result.stderr
    for kind in ['uboot', 'fip']:
        directory = project / f'cache/{kind}-alpha'
        assert subprocess.check_output(['/usr/bin/git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip() == expected[kind]
        assert not (directory / 'remote-advanced').exists()
    provenance = json.loads((project / 'device/vendor/alpha/upgrade/bootloader-sources.json').read_text())
    assert provenance['uboot']['commit'] == expected['uboot']
    assert provenance['fip']['commit'] == expected['fip']


def test_local_modifications_remain_intact_and_are_recorded_in_provenance(project):
    marker = project / 'cache/uboot-alpha/Makefile'
    marker.write_text(marker.read_text() + '# preserved local edit\n')
    result = build(project)
    assert result.returncode == 0, result.stdout + result.stderr
    assert marker.read_text().endswith('# preserved local edit\n')
    output = project / 'device/vendor/alpha/upgrade'
    provenance = json.loads((output / 'bootloader-sources.json').read_text())
    assert provenance['schema'] == 1
    assert provenance['uboot']['dirty'] is True
    assert provenance['uboot']['tracked_diff_sha256'] != hashlib.sha256(b'').hexdigest()
    assert provenance['fip']['dirty'] is False
    assert provenance['config_sha256'] == hashlib.sha256((project / 'board/vendor/alpha/u-boot.config').read_bytes()).hexdigest()
    assert provenance['resolved_config_sha256'] == hashlib.sha256((project / 'cache/uboot-alpha/.config').read_bytes()).hexdigest()


def test_current_station_recipe_uses_the_validated_published_source_commits():
    text = (ROOT / 'config/bootloaders/meson-sm1-yandex-station-max.conf').read_text()
    assert 'UBOOT_GIT_COMMIT="715b3e262019a786650454350d99f0b9af149701"' in text
    assert 'FIP_GIT_COMMIT="42d372123631066fb77fbcbb612dc3eb41a3f6f9"' in text


def test_default_source_caches_are_separated_by_pin_and_legacy_trees_are_not_replaced(project):
    # A valid legacy Station-style cache may be reused for its matching pin.
    legacy_uboot = project / 'cache/uboot'
    legacy_fip = project / 'cache/fip'
    legacy_uboot.symlink_to(project / 'cache/uboot-alpha', target_is_directory=True)
    legacy_fip.symlink_to(project / 'cache/fip-alpha', target_is_directory=True)
    assert build(project, 'speaker-alpha', explicit_cache=False).returncode == 0
    old_make = (legacy_uboot / 'make.calls').read_bytes()
    old_fip_status = subprocess.check_output(['/usr/bin/git', '-C', str(legacy_fip), 'status', '--porcelain'])
    # The second board requires different commits, so use new pin-keyed caches.
    result = build(project, 'speaker-beta', explicit_cache=False)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (legacy_uboot / 'make.calls').read_bytes() == old_make
    assert subprocess.check_output(['/usr/bin/git', '-C', str(legacy_fip), 'status', '--porcelain']) == old_fip_status
    provenance = json.loads((project / 'device/vendor/beta/upgrade/bootloader-sources.json').read_text())
    assert (project / ('cache/sources/uboot-' + provenance['uboot']['commit']) / 'Makefile').is_file()
    assert (project / ('cache/sources/fip-' + provenance['fip']['commit']) / 'build-fip.sh').is_file()
    assert legacy_uboot.is_symlink() and legacy_fip.is_symlink()


def test_failed_source_fetch_does_not_publish_a_partial_cache(project):
    source = project / 'cache/uboot-alpha'
    source.rename(project / 'saved-local-source')
    profile = project / 'config/bootloaders/meson-alpha.conf'
    text = profile.read_text().replace(source.as_uri(), (project / 'nonexistent-remote').as_uri())
    profile.write_text(text)
    result = build(project)
    assert result.returncode != 0
    assert 'Failed to fetch' in result.stdout
    assert not source.exists()
    assert not list((project / 'cache').glob('.atrios-git-fetch.*'))
    assert not (project / 'device').exists()
