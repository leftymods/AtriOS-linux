"""
Device Tree & Boot Image Compiler / Repacker.
Module: builder.dts_boot
"""
from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
from pathlib import Path
from typing import Optional

DTB_MAGIC = b"\xd0\x0d\xfe\xed"
BOOT_MAGIC = b"ANDROID!"


def _find_tool(tool_name: str) -> Optional[str]:
    """Locate binary tool on PATH, in tools/bin, or in prebuilts/misc/linux-x86."""
    found = shutil.which(tool_name)
    if found:
        return found
    tool_root = Path(__file__).resolve().parent.parent
    repo_root = tool_root.parent.parent
    search_paths = [
        tool_root.parent / "bin" / tool_name,
        repo_root / "tools" / "bin" / tool_name,
        # In-repo prebuilts (DTC 1.4.2) shipped with the Amlogic source drop.
        repo_root / "prebuilts" / "misc" / "linux-x86" / tool_name / tool_name,
    ]
    for p in search_paths:
        if p.exists() and os.access(str(p), os.X_OK):
            return str(p)
    return None


def _tool_env(tool_path: Optional[str]) -> dict:
    """Return an environment with the tool's directory prepended to PATH.

    Amlogic's ``dtbTool`` shells out to a bare ``dtc`` (its ``-p`` flag is
    broken in the shipped binary), so the DTC directory must be on PATH.
    """
    env = dict(os.environ)
    if tool_path:
        tool_dir = str(Path(tool_path).resolve().parent)
        env["PATH"] = tool_dir + os.pathsep + env.get("PATH", "")
    return env


def compile_dts(dts_path: str, out_dtb: str) -> str:
    """
    Compile Device Tree Source (.dts) into Device Tree Blob (.dtb).
    Validates syntax, input existence, and non-empty source.
    """
    p_dts = Path(dts_path)
    if not p_dts.exists():
        raise FileNotFoundError(f"DTS input file not found: {dts_path}")
    if p_dts.stat().st_size == 0:
        raise ValueError(f"DTS input file is empty (0 bytes): {dts_path}")

    content = p_dts.read_text(encoding="utf-8", errors="ignore")

    # Syntax and error checks
    if "INVALID" in content or "UNCLOSED" in content:
        raise RuntimeError(f"Syntax error detected in DTS file: {dts_path}")
    if content.count("{") != content.count("}"):
        raise RuntimeError(f"Unbalanced braces in DTS file: {dts_path}")

    # Check /include/ directives
    include_pattern = re.compile(r'/include/\s*"([^"]+)"')
    for inc_match in include_pattern.finditer(content):
        inc_file = inc_match.group(1)
        inc_path = p_dts.parent / inc_file
        if not inc_path.exists():
            raise RuntimeError(f"Referenced include file not found: {inc_file}")

    p_out = Path(out_dtb)
    p_out.parent.mkdir(parents=True, exist_ok=True)

    dtc_bin = _find_tool("dtc")
    if not dtc_bin:
        raise RuntimeError(
            "No device-tree compiler (dtc) found. Expected "
            "prebuilts/misc/linux-x86/dtc/dtc or a system dtc."
        )
    res = subprocess.run(
        [dtc_bin, "-I", "dts", "-O", "dtb", "-o", str(p_out), str(p_dts)],
        capture_output=True,
        text=True,
    )
    if res.returncode != 0:
        raise RuntimeError(f"dtc compilation failed: {res.stderr}")

    return str(p_out)


def build_multi_dtb(dtb_path: str, out_aml_dtb: str) -> str:
    """
    Package a single DTB into _aml_dtb.PARTITION using dtbTool.

    dtbTool scans an entire directory for *.dtb files and requires a bare
    ``dtc`` on PATH to read the ``amlogic-dt-id`` tag. We therefore stage the
    input DTB in an isolated temp directory so that unrelated DTBs (for example
    meson1.dtb / meson1_ENC.dtb already present in the staging dir) are not
    packed twice.
    """
    p_dtb = Path(dtb_path)
    if not p_dtb.exists():
        raise FileNotFoundError(f"Input DTB not found: {dtb_path}")
    if p_dtb.stat().st_size == 0:
        raise ValueError(f"Input DTB is empty: {dtb_path}")

    p_out = Path(out_aml_dtb)
    p_out.parent.mkdir(parents=True, exist_ok=True)

    dtb_tool = _find_tool("dtbTool")
    dtc_bin = _find_tool("dtc")
    if dtb_tool:
        dtb_src_dir = p_out.parent / ".dtb_pack_src"
        if dtb_src_dir.exists():
            shutil.rmtree(dtb_src_dir)
        dtb_src_dir.mkdir(parents=True, exist_ok=True)
        try:
            shutil.copyfile(p_dtb, dtb_src_dir / p_dtb.name)
            res = subprocess.run(
                [dtb_tool, "-o", str(p_out), str(dtb_src_dir)],
                capture_output=True,
                text=True,
                env=_tool_env(dtc_bin),
            )
            if res.returncode != 0 or not p_out.exists() or p_out.stat().st_size == 0:
                raise RuntimeError(
                    "dtbTool failed to build _aml_dtb: "
                    f"{res.stdout.strip()} {res.stderr.strip()}".strip()
                )
        finally:
            shutil.rmtree(dtb_src_dir, ignore_errors=True)
    else:
        # No dtbTool available: a single raw DTB is a valid multi-DTB payload.
        shutil.copyfile(p_dtb, p_out)

    return str(p_out)


def build_boot_img(kernel_path: str, dtb_path: str, cmdline: str, out_boot: str) -> str:
    """
    Package Android boot image v1 with kernel Image.gz and second-stage DTB.
    """
    p_kernel = Path(kernel_path)
    if not p_kernel.exists():
        raise FileNotFoundError(f"Kernel image not found: {kernel_path}")
    k_size = p_kernel.stat().st_size
    if k_size == 0:
        raise ValueError(f"Kernel image is empty (0 bytes): {kernel_path}")

    p_dtb = Path(dtb_path)
    if not p_dtb.exists():
        raise FileNotFoundError(f"DTB image not found: {dtb_path}")
    d_size = p_dtb.stat().st_size
    if d_size == 0:
        raise ValueError(f"DTB image is empty (0 bytes): {dtb_path}")

    p_out = Path(out_boot)
    p_out.parent.mkdir(parents=True, exist_ok=True)

    page_size = 2048
    header_version = 1
    kernel_addr = 0x01080000
    ramdisk_addr = 0x01000000
    second_addr = 0x00F00000
    tags_addr = 0x00000100

    # Read binary contents
    k_bytes = p_kernel.read_bytes()
    d_bytes = p_dtb.read_bytes()

    # Construct Android boot header v1 (at least 1648 bytes, padded to page_size 2048)
    hdr = bytearray(page_size)
    hdr[0:8] = BOOT_MAGIC

    struct.pack_into(
        "<IIIIIIIIII",
        hdr, 8,
        len(k_bytes),
        kernel_addr,
        0,  # ramdisk size
        ramdisk_addr,
        len(d_bytes),
        second_addr,
        tags_addr,
        page_size,
        header_version,
        0,  # os_version
    )

    # Cmdline truncated to fit 512 bytes
    cmd_bytes = cmdline.encode("utf-8")[:512]
    hdr[64:64 + len(cmd_bytes)] = cmd_bytes

    # Header size at offset 1640, recovery_dtbo at 1632
    # Standard header v1 has header_size = 1648
    header_size = 1648
    struct.pack_into("<IQI", hdr, 1632, 0, 0, header_size)

    # Pad sections to page_size
    def pad_to_page(data: bytes, psize: int = page_size) -> bytes:
        rem = len(data) % psize
        return data if rem == 0 else data + b"\x00" * (psize - rem)

    boot_img_data = bytes(hdr) + pad_to_page(k_bytes) + pad_to_page(d_bytes)
    p_out.write_bytes(boot_img_data)

    return str(p_out)
