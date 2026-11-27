"""Tests for system-wide modular profiles (OS distro, app flavor, modules, DSP)."""
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _run_bash(script: str, env_extra=None):
    env = os.environ.copy()
    env["PROJECT_ROOT"] = str(ROOT)
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        ["bash", "-euo", "pipefail", "-c", script],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
        timeout=30,
    )


def test_full_and_lite_profiles_resolve_distinct_os_flavor_and_modules():
    res_full = _run_bash(
        """
        source lib/general.sh
        source lib/config.sh
        load_board_config yndxsttion53 >/dev/null
        printf '%s|%s|%s|%s|%s|%s|%s\n' \
            "$ATRIOS_PROFILE" "$ROOTFS_DISTRO" "$ATRIOS_APP_FLAVOR" \
            "$AUDIO_AEC_ENGINE" "$AUDIO_NS_ENGINE" "$MOD_PIPEWIRE" "$MOD_DISPLAY"
        """
    )
    assert res_full.returncode == 0, res_full.stderr
    assert res_full.stdout.strip().splitlines()[-1] == "full|debian|full|pbfdaf|gtcrn|true|true"

    # Override profile to lite (Alpine + lite app + no PipeWire/display)
    res_lite = _run_bash(
        """
        source lib/general.sh
        source lib/config.sh
        ATRIOS_PROFILE=lite load_board_config yndxsttion53 >/dev/null
        printf '%s|%s|%s|%s|%s\n' \
            "$ATRIOS_PROFILE" "$ROOTFS_DISTRO" "$ATRIOS_APP_FLAVOR" \
            "$MOD_PIPEWIRE" "$MOD_DISPLAY"
        """
    )
    assert res_lite.returncode == 0, res_lite.stderr
    assert res_lite.stdout.strip().splitlines()[-1] == "lite|alpine|lite|false|false"


def test_pruner_strips_disabled_modules_and_writes_manifests(tmp_path):
    stage = tmp_path / "stage"
    for rel in [
        "etc/init.d/S50pipewire",
        "etc/init.d/S71atrios-bt-control",
        "etc/init.d/S80atriai",
        "etc/init.d/S85atrios-update",
        "usr/lib/atrios/screend",
        "usr/lib/atrios/rotaryd",
        "usr/lib/atrios/light",
        "usr/lib/atrios/ymusic",
        "usr/lib/atrios/libonnxruntime.so",
        "usr/share/atrios/models/gtcrn/gtcrn_simple.onnx",
        "usr/bin/atri-mp3dec",
    ]:
        p = stage / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("stub\n")

    res = _run_bash(
        f"""
        source lib/general.sh
        source lib/config.sh
        source lib/modules.sh
        ATRIOS_PROFILE=lite load_board_config yndxsttion53 >/dev/null
        AUDIO_AEC_ENGINE=nlms
        AUDIO_NS_ENGINE=bnr
        prune_disabled_rootfs_modules "{stage}"
        write_hardware_manifest "{stage}"
        write_modules_manifest "{stage}"
        """
    )
    assert res.returncode == 0, res.stderr

    # Disabled in lite profile -> must be removed
    assert not (stage / "etc/init.d/S50pipewire").exists()
    assert not (stage / "usr/lib/atrios/screend").exists()
    assert not (stage / "usr/lib/atrios/rotaryd").exists()
    assert not (stage / "usr/lib/atrios/libonnxruntime.so").exists()
    assert not (stage / "usr/share/atrios/models/gtcrn").exists()

    # Enabled in lite profile -> must remain
    assert (stage / "etc/init.d/S80atriai").exists()
    assert (stage / "etc/init.d/S71atrios-bt-control").exists()
    assert (stage / "etc/init.d/S85atrios-update").exists()
    assert (stage / "usr/lib/atrios/light").exists()
    assert (stage / "usr/lib/atrios/ymusic").exists()
    assert (stage / "usr/bin/atri-mp3dec").exists()

    modules = json.loads((stage / "etc/atrios/modules.json").read_text())
    assert modules["board"] == "yndxsttion53"
    assert modules["profile"] == "lite"
    assert modules["rootfs"]["distro"] == "alpine"
    assert modules["app"]["flavor"] == "lite"
    assert modules["audio_dsp"]["aec_engine"] == "nlms"
    assert modules["audio_dsp"]["ns_engine"] == "bnr"
    assert modules["modules"]["pipewire"] is False
    assert modules["modules"]["display"] is False
    assert modules["modules"]["ota"] is True


def test_compile_list_and_info_show_modular_profiles():
    res_list = subprocess.run(
        ["bash", str(ROOT / "compile.sh"), "list"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=20,
    )
    assert res_list.returncode == 0, res_list.stderr
    assert "yndxsttion53" in res_list.stdout
    assert "debian" in res_list.stdout

    res_info = subprocess.run(
        ["bash", str(ROOT / "compile.sh"), "BOARD=yndxsttion53", "TARGET=info"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=20,
    )
    assert res_info.returncode == 0, res_info.stderr
    assert "System Profile:  full" in res_info.stdout
    assert "RootFS Distro:   debian" in res_info.stdout
    assert "AtriOS Flavor:   full" in res_info.stdout
    assert "aec=pbfdaf, ns=gtcrn" in res_info.stdout
