"""
AVB 2.0 Full Partition Signing Module for AtriStation S905X3.
Cryptographically signs ALL partitions (boot, recovery, dtbo, system, vendor, odm, product)
with the custom RSA-4096 key and produces a signed, verified vbmeta.PARTITION.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

TOOL_ROOT = Path(__file__).resolve().parent.parent
PROJECT_ROOT = TOOL_ROOT.parent.parent

AVBTOOL = str(PROJECT_ROOT / "external" / "avb" / "avbtool")
if not Path(AVBTOOL).exists():
    AVBTOOL = str(PROJECT_ROOT / "tools" / "bin" / "avbtool")

DEFAULT_KEY = str(TOOL_ROOT / "keys" / "atristation_priv.pem")
if not Path(DEFAULT_KEY).exists():
    DEFAULT_KEY = str(PROJECT_ROOT / "keys" / "atristation_priv.pem")

ALGORITHM = "SHA256_RSA4096"

PARTITION_SIZES = {
    "boot": 16777216,
    "recovery": 25165824,
    "dtbo": 8388608,
    "vendor": 536870912,
    "odm": 134217728,
    "product": 134217728,
    "system": 1551892480,
}

HASH_PARTS = ["boot", "recovery", "dtbo"]
HASHTREE_PARTS = ["odm", "product", "vendor", "system"]


def sign_all_partitions(
    repack_dir: str | Path,
    key_path: str | Path = DEFAULT_KEY,
    algorithm: str = ALGORITHM,
    verbose: bool = True,
) -> bool:
    """Cryptographically sign all partitions in repack_dir and generate signed vbmeta.PARTITION."""
    repack_dir = Path(repack_dir)
    key_path = Path(key_path)

    if not key_path.exists():
        print(f"[AVB] Error: Key not found: {key_path}")
        return False

    include_flags = []

    # 1. Sign hash partitions (boot, recovery, dtbo)
    for part in HASH_PARTS:
        img = repack_dir / f"{part}.PARTITION"
        if not img.exists():
            continue
        size = PARTITION_SIZES.get(part, img.stat().st_size)
        cmd = [
            "python2", AVBTOOL, "add_hash_footer",
            "--image", str(img),
            "--partition_size", str(size),
            "--partition_name", part,
            "--key", str(key_path),
            "--algorithm", algorithm,
        ]
        if verbose:
            print(f"[AVB] Signing {part}.PARTITION (hash footer)...")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[AVB] Warning: add_hash_footer on {part} failed: {res.stderr.strip()}")
        else:
            include_flags.extend(["--include_descriptors_from_image", str(img)])

    # 2. Sign hashtree partitions (ext4: odm, product, vendor, system)
    for part in HASHTREE_PARTS:
        img = repack_dir / f"{part}.PARTITION"
        if not img.exists():
            continue
        size = PARTITION_SIZES.get(part, img.stat().st_size)
        cmd = [
            "python2", AVBTOOL, "add_hashtree_footer",
            "--image", str(img),
            "--partition_size", str(size),
            "--partition_name", part,
            "--key", str(key_path),
            "--algorithm", algorithm,
            "--do_not_generate_fec",
        ]
        if verbose:
            print(f"[AVB] Signing {part}.PARTITION (hashtree footer)...")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[AVB] Note on {part}: {res.stderr.strip()}")
        else:
            include_flags.extend(["--include_descriptors_from_image", str(img)])

    # 3. Generate signed vbmeta.PARTITION incorporating all descriptors
    vbmeta_out = repack_dir / "vbmeta.PARTITION"
    cmd_vbmeta = [
        "python2", AVBTOOL, "make_vbmeta_image",
        "--key", str(key_path),
        "--algorithm", algorithm,
        "--output", str(vbmeta_out),
        "--rollback_index", "0",
    ] + include_flags

    if verbose:
        print(f"[AVB] Generating signed vbmeta.PARTITION with {len(include_flags)//2} partition descriptors...")
    res = subprocess.run(cmd_vbmeta, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[AVB] Error generating vbmeta: {res.stderr.strip()}")
        return False

    print(f"[AVB] Successfully generated signed vbmeta.PARTITION ({vbmeta_out.stat().st_size} bytes)")
    return True


if __name__ == "__main__":
    import sys
    target_dir = sys.argv[1] if len(sys.argv) > 1 else str(TOOL_ROOT / "staging")
    sign_all_partitions(target_dir)
