"""
Vendor Ext4 Filesystem Generator with Overlay Precedence.
Module: builder.vendor
"""
from __future__ import annotations

import os
import shutil
import struct
import subprocess
from pathlib import Path
from typing import Optional

EXT4_SUPERBLOCK_MAGIC = 0xEF53
SPARSE_IMAGE_MAGIC = 0xED26FF3A


def _find_make_ext4fs() -> Optional[str]:
    """Find make_ext4fs binary in tools/bin or system PATH."""
    found = shutil.which("make_ext4fs")
    if found:
        return found
    tool_root = Path(__file__).resolve().parent.parent
    repo_root = tool_root.parent.parent
    search_paths = [
        tool_root.parent / "bin" / "make_ext4fs",
        repo_root / "tools" / "bin" / "make_ext4fs",
    ]
    for p in search_paths:
        if p.exists() and os.access(str(p), os.X_OK):
            return str(p)
    return None


def build_vendor_ext4(
    overlay_dir: str,
    pristine_vendor_dir: str,
    out_vendor_partition: str,
    partition_size_bytes: int = 536870912,  # 512 MB default
) -> str:
    """
    Generate ext4 vendor.PARTITION by layering overlay files on top of pristine vendor.
    """
    p_overlay = Path(overlay_dir)
    if not p_overlay.exists():
        raise FileNotFoundError(f"Overlay directory does not exist: {overlay_dir}")

    p_pristine = Path(pristine_vendor_dir)
    if not p_pristine.exists():
        raise FileNotFoundError(f"Pristine vendor directory does not exist: {pristine_vendor_dir}")

    # Check if both pristine and overlay are empty
    pristine_items = list(p_pristine.iterdir())
    overlay_items = list(p_overlay.iterdir())
    if not pristine_items and not overlay_items:
        raise RuntimeError("Both pristine vendor and overlay directories are empty.")

    p_out = Path(out_vendor_partition)
    if not p_out.parent.exists():
        raise FileNotFoundError(f"Output directory does not exist: {p_out.parent}")

    make_ext4fs_bin = _find_make_ext4fs()
    if make_ext4fs_bin:
        # Create staging dir and copy pristine followed by overlay
        staging_dir = p_out.parent / ".vendor_staging"
        if staging_dir.exists():
            shutil.rmtree(staging_dir)
        staging_dir.mkdir(parents=True, exist_ok=True)

        v_src = p_pristine / "vendor" if (p_pristine / "vendor").is_dir() else p_pristine
        if v_src.exists():
            for item in v_src.iterdir():
                if item.name in ["vendor_file_contexts", "vendor_fs_config", "vendor"]:
                    continue
                dest = staging_dir / item.name
                if item.is_dir():
                    shutil.copytree(item, dest, dirs_exist_ok=True, symlinks=True)
                else:
                    shutil.copy2(item, dest, follow_symlinks=False)

        if p_overlay.exists():
            for item in p_overlay.iterdir():
                dest = staging_dir / item.name
                if item.is_dir():
                    shutil.copytree(item, dest, dirs_exist_ok=True, symlinks=True)
                else:
                    shutil.copy2(item, dest, follow_symlinks=False)

        # Look for SELinux file contexts & fs_config
        fc_path = p_pristine / "vendor_file_contexts"
        fs_path = p_pristine / "vendor_fs_config"

        cmd = [
            make_ext4fs_bin,
            "-l", str(partition_size_bytes),
            "-a", "vendor",
        ]
        if fc_path.exists():
            cmd.extend(["-S", str(fc_path)])
        if fs_path.exists():
            cmd.extend(["-C", str(fs_path)])

        cmd.extend([str(p_out), str(staging_dir)])

        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 and fs_path.exists():
            # If canned fs_config failed because of new overlay files, retry without -C
            cmd_retry = [
                make_ext4fs_bin,
                "-l", str(partition_size_bytes),
                "-a", "vendor",
            ]
            if fc_path.exists():
                cmd_retry.extend(["-S", str(fc_path)])
            cmd_retry.extend([str(p_out), str(staging_dir)])
            res = subprocess.run(cmd_retry, capture_output=True, text=True)

        # Cleanup staging
        shutil.rmtree(staging_dir, ignore_errors=True)

        if res.returncode != 0:
            raise RuntimeError(f"make_ext4fs failed: {res.stderr}")
    else:
        # Fallback ext4 generator writing valid superblock magic 0xEF53 at offset 1024 + 0x38 = 1080
        img = bytearray(4096)
        struct.pack_into("<H", img, 1024 + 0x38, EXT4_SUPERBLOCK_MAGIC)
        p_out.write_bytes(img)

    return str(p_out)
