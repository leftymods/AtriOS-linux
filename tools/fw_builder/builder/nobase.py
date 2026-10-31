"""
From-Scratch Staging Synthesis (no stock base image required).
Module: builder.nobase

Builds a complete Amlogic v2 upgrade staging directory from assets that are
already present in the repository, so no external firmware image is needed:

    DDR.USB / UBOOT.USB / bootloader.PARTITION
        <- device/yandex/station_max/upgrade/bootloader.img
    aml_sdc_burn.UBOOT
        <- device/yandex/station_max/upgrade/u-boot.bin.sd.bin
    platform.conf, aml_sdc_burn.ini, logo.PARTITION
        <- device/yandex/station_max/upgrade/
    meson1.dtb / meson1_ENC.dtb / _aml_dtb.PARTITION
        <- compiled tools/fw_builder/overlay/dts/sm1_s905x3_yandex.dts
    boot.PARTITION
        <- overlay/boot/Image.gz + compiled DTB + overlay/boot/cmdline.txt
    system.PARTITION
        <- pre-built staging/system.PARTITION, or overlay/system/ via
           make_ext4fs, or a minimal placeholder as a last resort

The ENC (encrypted) variants are intentionally not produced: the Linux build
does not enable secure boot and ``ampack pack`` does not require them.
"""
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Tuple

TOOL_ROOT = Path(__file__).resolve().parent.parent          # tools/fw_builder
REPO_ROOT = TOOL_ROOT.parent.parent                          # AtriOS-linux
DEVICE_UPGRADE_DIR = REPO_ROOT / "device" / "yandex" / "station_max" / "upgrade"

BOOTLOADER_IMG = DEVICE_UPGRADE_DIR / "bootloader.img"
SD_UBOOT_IMG = DEVICE_UPGRADE_DIR / "u-boot.bin.sd.bin"
BOOTLOADER_ENC_IMG = DEVICE_UPGRADE_DIR / "bootloader.img.encrypt"
SD_UBOOT_ENC_IMG = DEVICE_UPGRADE_DIR / "u-boot.bin.encrypt.sd.bin"
PLATFORM_CONF = DEVICE_UPGRADE_DIR / "platform.conf"
SD_BURN_INI = DEVICE_UPGRADE_DIR / "aml_sdc_burn.ini"
LOGO_IMG = DEVICE_UPGRADE_DIR / "logo.PARTITION"

DEFAULT_DTS = TOOL_ROOT / "overlay" / "dts" / "sm1_s905x3_yandex.dts"
DEFAULT_KERNEL = TOOL_ROOT / "overlay" / "boot" / "Image.gz"
DEFAULT_CMDLINE = TOOL_ROOT / "overlay" / "boot" / "cmdline.txt"
DEFAULT_SYSTEM_TREE = TOOL_ROOT / "overlay" / "system"

# Matches the "system" partition declared in the ported DTS (0x5c800000).
SYSTEM_PARTITION_SIZE = 1551892480
# Smallest ext4 that make_ext4fs can produce without a journal.
PLACEHOLDER_PARTITION_SIZE = 8 * 1024 * 1024


def _find_make_ext4fs() -> Optional[str]:
    """Locate the in-repo make_ext4fs binary (or a system one)."""
    found = shutil.which("make_ext4fs")
    if found:
        return found
    for candidate in (
        REPO_ROOT / "tools" / "bin" / "make_ext4fs",
        TOOL_ROOT.parent / "bin" / "make_ext4fs",
    ):
        if candidate.exists() and candidate.is_file():
            return str(candidate)
    return None


def required_assets() -> List[Path]:
    """Assets that must exist for a from-scratch build."""
    return [
        BOOTLOADER_IMG,
        SD_UBOOT_IMG,
        PLATFORM_CONF,
        SD_BURN_INI,
        LOGO_IMG,
        DEFAULT_DTS,
        DEFAULT_KERNEL,
        DEFAULT_CMDLINE,
    ]


def missing_assets() -> List[Path]:
    """Return the subset of required assets that is absent."""
    return [p for p in required_assets() if not p.exists()]


def assets_available() -> Tuple[bool, List[str]]:
    """Return (available, list_of_human_readable_missing_paths)."""
    missing = missing_assets()
    return (not missing), [str(p) for p in missing]


def _copy(src: Path, dst: Path) -> None:
    if not src.exists():
        raise FileNotFoundError(f"Required asset not found: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def _tree_mtime(root: Path) -> float:
    """Newest mtime found anywhere under *root* (0 when empty)."""
    newest = 0.0
    for path in root.rglob("*"):
        try:
            newest = max(newest, path.stat().st_mtime)
        except OSError:
            pass
    return newest


def _build_system_partition(staging_dir: Path, work_dir: Path, verbose: bool = False) -> str:
    """Create system.PARTITION from overlay/system, or fall back to a placeholder."""
    target = staging_dir / "system.PARTITION"
    # Marker written by build_atrios.sh (or by us) proving that an existing
    # system.PARTITION really is an AtriOS rootfs and not a leftover Android
    # image. It lives next to staging/, never inside it, so it cannot be
    # packed into the firmware image.
    marker = staging_dir.parent / ".system_from_rootfs"

    make_ext4fs = _find_make_ext4fs()
    tree_ready = DEFAULT_SYSTEM_TREE.exists() and any(DEFAULT_SYSTEM_TREE.iterdir())

    # 1. A real ext4 produced by build_atrios.sh from the Buildroot rootfs.
    if target.exists() and target.stat().st_size > 0 and marker.exists():
        if not tree_ready or target.stat().st_mtime >= _tree_mtime(DEFAULT_SYSTEM_TREE):
            if verbose:
                print(f"[BUILD] Reusing existing AtriOS system.PARTITION ({target.stat().st_size:,} bytes)")
            return str(target)

    # 2. Build a real ext4 from the Buildroot rootfs tree, if populated.
    if tree_ready and make_ext4fs:
        if verbose:
            print(f"[BUILD] Building system.PARTITION from {DEFAULT_SYSTEM_TREE} ...")
        res = subprocess.run(
            [
                make_ext4fs,
                "-l", str(SYSTEM_PARTITION_SIZE),
                "-a", "system",
                str(target),
                str(DEFAULT_SYSTEM_TREE),
            ],
            capture_output=True,
            text=True,
        )
        if res.returncode == 0 and target.exists():
            marker.touch()
            return str(target)
        print(f"[WARN] make_ext4fs failed to build system.PARTITION: {res.stderr.strip()}")

    # 3. An existing partition without a marker is of unknown origin. Never ship
    #    it: it may be the stock Android image reused by a previous build.
    if target.exists() and not marker.exists():
        print("[WARN] Ignoring unmarked system.PARTITION (stale stock/Android image).")
        target.unlink()

    # 4. Last resort: a minimal, mountable ext4 placeholder so the package can
    #    still be assembled and verified. It is NOT bootable.
    print(
        "[WARN] No Buildroot rootfs found (overlay/system is empty). "
        "Creating a placeholder system.PARTITION - the resulting image will "
        "pack and verify but will not boot into AtriOS."
    )
    placeholder_tree = work_dir / "system_placeholder"
    placeholder_tree.mkdir(parents=True, exist_ok=True)
    (placeholder_tree / "PLACEHOLDER").write_text(
        "This system.PARTITION is a placeholder generated by fw_builder.\n"
        "Build the Buildroot rootfs and re-run the build to produce a bootable image.\n",
        encoding="utf-8",
    )
    try:
        if make_ext4fs:
            res = subprocess.run(
                [
                    make_ext4fs,
                    "-J",
                    "-l", str(PLACEHOLDER_PARTITION_SIZE),
                    "-a", "system",
                    str(target),
                    str(placeholder_tree),
                ],
                capture_output=True,
                text=True,
            )
            if res.returncode == 0 and target.exists():
                return str(target)
            print(f"[WARN] make_ext4fs placeholder failed: {res.stderr.strip()}")
    finally:
        shutil.rmtree(placeholder_tree, ignore_errors=True)

    # 5. Zero-filled file of the same nominal size (still packable/verifiable).
    target.write_bytes(b"\x00" * PLACEHOLDER_PARTITION_SIZE)
    return str(target)


def synthesize_staging(
    staging_dir: Path,
    workspace: Optional[Path] = None,
    verbose: bool = False,
) -> Dict[str, str]:
    """
    Populate ``staging_dir`` with every item ``ampack pack`` needs, using only
    in-repo assets. Returns a mapping of item name -> absolute path.

    ``workspace`` defaults to TOOL_ROOT and is used to resolve overlay files
    the same way the incremental build does.
    """
    ws = Path(workspace) if workspace else TOOL_ROOT
    staging = Path(staging_dir)
    staging.mkdir(parents=True, exist_ok=True)
    work_dir = staging.parent / ".nobase_work"
    work_dir.mkdir(parents=True, exist_ok=True)

    missing = missing_assets()
    if missing:
        raise FileNotFoundError(
            "Cannot build without the stock image; missing repo assets:\n  "
            + "\n  ".join(str(p) for p in missing)
        )

    from builder.dts_boot import build_boot_img, build_multi_dtb, compile_dts

    produced: Dict[str, str] = {}

    def record(name: str, path: Path) -> None:
        produced[name] = str(path.resolve())

    # --- Bootloader / USB burning items -------------------------------------
    _copy(BOOTLOADER_IMG, staging / "DDR.USB")
    _copy(BOOTLOADER_IMG, staging / "UBOOT.USB")
    _copy(BOOTLOADER_IMG, staging / "bootloader.PARTITION")
    _copy(SD_UBOOT_IMG, staging / "aml_sdc_burn.UBOOT")
    _copy(PLATFORM_CONF, staging / "platform.conf")
    _copy(SD_BURN_INI, staging / "aml_sdc_burn.ini")
    _copy(LOGO_IMG, staging / "logo.PARTITION")
    for name in (
        "DDR.USB",
        "UBOOT.USB",
        "bootloader.PARTITION",
        "aml_sdc_burn.UBOOT",
        "platform.conf",
        "aml_sdc_burn.ini",
        "logo.PARTITION",
    ):
        record(name, staging / name)

    if BOOTLOADER_ENC_IMG.exists():
        _copy(BOOTLOADER_ENC_IMG, staging / "DDR_ENC.USB")
        _copy(BOOTLOADER_ENC_IMG, staging / "UBOOT_ENC.USB")
        record("DDR_ENC.USB", staging / "DDR_ENC.USB")
        record("UBOOT_ENC.USB", staging / "UBOOT_ENC.USB")

    if SD_UBOOT_ENC_IMG.exists():
        _copy(SD_UBOOT_ENC_IMG, staging / "aml_sdc_burn.UBOOT.ENC")
        record("aml_sdc_burn.UBOOT.ENC", staging / "aml_sdc_burn.UBOOT.ENC")

    # --- Device tree --------------------------------------------------------
    dts_file = ws / "overlay" / "dts" / DEFAULT_DTS.name
    if not dts_file.exists():
        dts_file = DEFAULT_DTS
    raw_dtb = work_dir / f"{dts_file.stem}.dtb"
    compile_dts(str(dts_file), str(raw_dtb))

    aml_dtb = staging / "_aml_dtb.PARTITION"
    build_multi_dtb(str(raw_dtb), str(aml_dtb))
    _copy(aml_dtb, staging / "meson1.dtb")
    _copy(aml_dtb, staging / "meson1_ENC.dtb")
    _copy(raw_dtb, staging / f"{dts_file.stem}.dtb")
    record("_aml_dtb.PARTITION", aml_dtb)
    record("meson1.dtb", staging / "meson1.dtb")
    record("meson1_ENC.dtb", staging / "meson1_ENC.dtb")
    record(f"{dts_file.stem}.dtb", staging / f"{dts_file.stem}.dtb")

    # --- Boot image ---------------------------------------------------------
    kernel = ws / "overlay" / "boot" / "Image.gz"
    if not kernel.exists():
        kernel = DEFAULT_KERNEL
    cmdline_file = ws / "overlay" / "boot" / "cmdline.txt"
    if not cmdline_file.exists():
        cmdline_file = DEFAULT_CMDLINE
    cmdline = cmdline_file.read_text(encoding="utf-8").strip()
    boot_part = staging / "boot.PARTITION"
    build_boot_img(str(kernel), str(raw_dtb), cmdline, str(boot_part))
    record("boot.PARTITION", boot_part)

    # --- System partition ---------------------------------------------------
    system_part = Path(_build_system_partition(staging, work_dir, verbose=verbose))
    record("system.PARTITION", system_part)

    # Drop anything left in staging by earlier builds (stock ENC blobs,
    # intermediate DTBs, marker files). Only synthesized items may be packed.
    keep = {Path(p).name for p in produced.values()}
    for entry in sorted(staging.iterdir()):
        if entry.name in keep:
            continue
        if entry.is_dir():
            shutil.rmtree(entry, ignore_errors=True)
        else:
            entry.unlink()
        if verbose:
            print(f"[BUILD] Removed stale staging item: {entry.name}")

    if verbose:
        print(f"[BUILD] From-scratch staging populated: {staging}")
        for name in sorted(produced):
            size = Path(produced[name]).stat().st_size
            print(f"[BUILD]   {name:<22} {size:>12,} bytes")

    return produced
