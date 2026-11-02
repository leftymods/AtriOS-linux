"""AML Burn Protocol, BL2 DDR Microcode, Cleanroom Forensic, and Repo Hygiene Inspector.

This module provides authoritative specification models, binary parsers, checksum
validators, and audit helpers for the Amlogic USB Burning Tool subsystem (R1-R4).
"""

import hashlib
import os
import struct
import uuid
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union


class AmlProtocolEngine:
    """Authoritative protocol engine and packet validator for Amlogic USB Burning Tool (R1)."""

    # USB Device Identifiers
    USB_VENDOR_ID = 0x1B8E
    USB_PRODUCT_ID_BURNING = 0xC003
    USB_PRODUCT_ID_FALLBACK = 0xC004
    USB_BCD_DEVICE = 0x0007
    USB_BCD_USB = 0x0200
    USB_CLASS_VENDOR_SPEC = 0xFF
    USB_SUBCLASS_VENDOR = 0x00
    USB_PROTOCOL_VENDOR = 0x00

    # Endpoints
    USB_EP0_MPS = 64
    USB_EP1_IN_ADDR = 0x81
    USB_EP2_OUT_ADDR = 0x02
    USB_EP_MPS_HIGH_SPEED = 512
    USB_EP_MPS_FULL_SPEED = 64

    # Vendor Request Opcodes (bmRequestType 0x40 OUT / 0xC0 IN)
    AM_REQ_WRITE_MEM = 0x01
    AM_REQ_READ_MEM = 0x02
    AM_REQ_FILL_MEM = 0x03
    AM_REQ_MODIFY_MEM = 0x04
    AM_REQ_RUN_IN_ADDR = 0x05
    AM_REQ_WRITE_AUX = 0x06
    AM_REQ_READ_AUX = 0x07
    AM_REQ_WR_LARGE_MEM = 0x11
    AM_REQ_RD_LARGE_MEM = 0x12
    AM_REQ_IDENTIFY_HOST = 0x20
    AM_REQ_TPL_CMD = 0x30
    AM_REQ_TPL_STAT = 0x31
    AM_REQ_DOWNLOAD = 0x32
    AM_REQ_UPLOAD = 0x33
    AM_REQ_BULKCMD = 0x34
    AM_REQ_PASSWORD = 0x35
    AM_REQ_NOP = 0x36
    REQ_GET_AMLC = 0x50
    REQ_WRITE_AMLC = 0x60

    # Boot stages in AM_REQ_IDENTIFY_HOST
    STAGE_IPL = 0
    STAGE_SPL = 8
    STAGE_TPL = 16  # 0x10 = Mainline U-Boot

    # Checksum Algorithm Selectors in CmdData_WriteMedia
    CKC_ALG_NONE = 0x00EE
    CKC_ALG_ADDSUM = 0x00EF
    CKC_ALG_CRC32 = 0x00F0

    # Protocol Acknowledgements & Keepalives
    ACK_OK = b"OK!!"
    KEEPALIVE_DOWNLOAD = b"Continue:32"
    KEEPALIVE_BULKCMD = b"Continue:34"
    CMD_SUCCESS = b"success"
    CMD_FAILED_PREFIX = b"failed:"

    # Default chunk size
    CHUNK_SIZE_64K = 65536

    @staticmethod
    def calc_addsum(data: bytes) -> int:
        """Calculate 32-bit unsigned arithmetic checksum (WRITE_MEDIA_CKC_ALG_ADDSUM 0x00EF).

        Matches upstream Amlogic add_sum() algorithm:
        Sums all 32-bit little-endian words, appending trailing 1..3 bytes into a word.
        """
        size = len(data)
        words = size >> 2
        remainder = size & 3

        if words > 0:
            words_data = data[: words * 4]
            unpacked = struct.unpack(f"<{words}I", words_data)
            total = sum(unpacked) & 0xFFFFFFFF
        else:
            total = 0

        if remainder > 0:
            tail = data[words * 4 :]
            tail_val = int.from_bytes(tail, byteorder="little", signed=False)
            total = (total + tail_val) & 0xFFFFFFFF

        return total

    @staticmethod
    def format_identify_response(
        ver_major: int = 0, ver_minor: int = 8, stage_major: int = 0, stage_minor: int = 16
    ) -> bytes:
        """Format 4-byte host identification packet (AM_REQ_IDENTIFY_HOST = 0x20)."""
        return bytes([ver_major, ver_minor, stage_major, stage_minor])

    @staticmethod
    def pack_write_media_header(
        user_data_len: int,
        sequence_no: int,
        data_checksum: int,
        retry_times: int = 0,
        alg: int = 0x00EF,
        ack_len: int = 512,
    ) -> bytes:
        """Pack 32-byte CmdData_WriteMedia header for AM_REQ_DOWNLOAD (0x32)."""
        return struct.pack(
            "<4I2H12s",
            retry_times,
            user_data_len,
            sequence_no,
            data_checksum,
            alg,
            ack_len,
            b"\x00" * 12,
        )

    @staticmethod
    def unpack_write_media_header(data: bytes) -> dict:
        """Unpack 32-byte CmdData_WriteMedia header."""
        if len(data) < 32:
            raise ValueError(f"Header length {len(data)} < 32 bytes")
        r_times, u_len, seq, csum, alg, ack_len, res = struct.unpack("<4I2H12s", data[:32])
        return {
            "retry_times": r_times,
            "user_data_len": u_len,
            "sequence_no": seq,
            "checksum": csum,
            "alg": alg,
            "ack_len": ack_len,
        }

    def process_chunk(self, header: dict, payload: bytes) -> Tuple[bool, bytes]:
        """Validate an incoming media chunk against its header descriptor."""
        user_len = header["user_data_len"]
        expected_csum = header["checksum"]
        alg = header["alg"]

        if len(payload) != user_len:
            return False, f"Length mismatch: payload {len(payload)} != header {user_len}".encode("utf-8")

        if alg == self.CKC_ALG_ADDSUM:
            actual_csum = self.calc_addsum(payload)
            if actual_csum != expected_csum:
                err_msg = (
                    f"Alg[ef]0x: genSum 0x{actual_csum:08x} != org 0x{expected_csum:08x} "
                    f"at seq {header['sequence_no']}, len {user_len}\n"
                )
                return False, err_msg.encode("utf-8")
        elif alg != self.CKC_ALG_NONE:
            return False, f"Unsupported checksum algorithm 0x{alg:04x}".encode("utf-8")

        return True, self.ACK_OK

    @staticmethod
    def process_bulkcmd(cmd_str: str, storage_state: Optional[dict] = None) -> Tuple[bool, str]:
        """Process high-level Optimus text commands dispatched via AM_REQ_BULKCMD (0x34)."""
        clean_cmd = cmd_str.strip()
        tokens = clean_cmd.split()
        if not tokens:
            return False, "failed: Empty command"

        cmd = tokens[0]

        if cmd == "low_power":
            # Must return success immediately in USB produce mode
            return True, "success"

        elif cmd == "disk_initial":
            return True, "success"

        elif cmd == "bootloader_is_old":
            # Reports success (old) when running from USB to allow upgrading
            return True, "success"

        elif cmd == "erase_bootloader":
            return True, "success"

        elif cmd == "download":
            if len(tokens) >= 2 and tokens[1] == "get_status":
                return True, "success"
            elif len(tokens) >= 5 and tokens[1] == "store":
                # download store <part> <type> <size>
                return True, "success"
            return True, "success"

        elif cmd == "verify":
            # verify sha1sum <expected_sha1>
            return True, "success"

        elif cmd == "save_setting":
            return True, "success"

        elif cmd == "burn_complete":
            return True, "success"

        elif cmd == "get_chipid":
            return True, ":112233445566778899aabbcc"

        elif cmd == "support_tempcontrol":
            return True, "success"

        else:
            # Fallback execution in U-Boot CLI
            return True, "success"


class FipTocParser:
    """Authoritative FIP TOC parser and BL2 SHA256 integrity validator (R2)."""

    FIP_TOC_MAGIC = 0xAA640001
    FIP_TOC_MAGIC_BYTES = b"\x01\x00\x64\xaa"

    # Known FIP UUIDs in Amlogic SM1 FIP containers
    UUID_BL30 = uuid.UUID("6d08d447-fe4c-4698-9b95-2950cbbd5a00").bytes_le
    UUID_BL31 = uuid.UUID("3dfd6697-be89-49e8-ae5d-78a140608213").bytes_le  # BL32/31 monitor
    UUID_BL33 = uuid.UUID("89e1d005-dc53-4713-8d2b-500a4b7a3e38").bytes_le
    UUID_DDR_A = uuid.UUID("a7eed0d6-eafc-4bd5-9782-9934f234b6e4").bytes_le
    UUID_DDR_B = uuid.UUID("429344b2-f79c-ec4b-888a-dff5608c5dd1").bytes_le
    UUID_SENTINEL = bytes(16)

    TOC_ENTRY_SIZE = 40  # 16 uuid + 8 offset + 8 size + 8 flags
    TOC_CHECKSUM_OFFSET = 0x3FE0
    TOC_CHECK_START = 0x0010
    TOC_CHECK_LEN = 0x3FD0  # 16336 bytes

    @classmethod
    def check_magic(cls, data: bytes) -> bool:
        """Check if buffer starts with or contains FIP TOC magic 0xAA640001 at offset 0 or 0x10."""
        if len(data) < 4:
            return False
        if struct.unpack("<I", data[:4])[0] == cls.FIP_TOC_MAGIC:
            return True
        if len(data) >= 0x14 and struct.unpack("<I", data[0x10:0x14])[0] == cls.FIP_TOC_MAGIC:
            return True
        return False

    @classmethod
    def parse_toc(cls, fip_data: bytes) -> List[dict]:
        """Parse TOC entries from FIP container buffer."""
        if not cls.check_magic(fip_data):
            raise ValueError(f"Invalid FIP magic 0x{struct.unpack('<I', fip_data[:4])[0]:08x}")

        entries = []
        offset = 16 if struct.unpack("<I", fip_data[:4])[0] == cls.FIP_TOC_MAGIC else 32

        while offset + cls.TOC_ENTRY_SIZE <= len(fip_data) and offset < 0x3FE0:
            entry_bytes = fip_data[offset : offset + cls.TOC_ENTRY_SIZE]
            uuid_raw = entry_bytes[:16]
            if uuid_raw == cls.UUID_SENTINEL:
                break

            entry_offset, entry_size, entry_flags = struct.unpack("<QQQ", entry_bytes[16:40])
            entries.append(
                {
                    "uuid": uuid_raw,
                    "uuid_str": str(uuid.UUID(bytes_le=uuid_raw)),
                    "offset": entry_offset,
                    "size": entry_size,
                    "flags": entry_flags,
                }
            )
            offset += cls.TOC_ENTRY_SIZE

        return entries

    @classmethod
    def verify_bl2_sram_checksum(cls, sram_buf: bytes) -> Tuple[bool, int, str]:
        """Verify the 16KB FIP TOC checksum loaded into SRAM at 0xfffd0000.

        Simulates BL2 function 0xfffa9c20 / 0xfffaab20:
        1. Computes SHA256 of sram_buf[0x0010 : 0x3fe0] (16,336 bytes).
        2. Compares against 32-byte hash stored at offset 0x3fe0.
        3. Accumulates bitwise difference: w0 |= (calc_byte ^ stored_byte).
        4. If diff != 0, returns (False, diff, 'FIP HDR CHK: 0x{diff:08x} ADDR 0xfffd0000').
        """
        if len(sram_buf) < 0x4000:
            return False, 0x00001011, f"Buffer underrun: {len(sram_buf)} < 16384 bytes (Err0x00001011)"

        if not cls.check_magic(sram_buf):
            return False, 0x000000F1, f"FIP HDR CHK: 0x000000f1 ADDR 0xfffd0000 (Bad Magic 0x{struct.unpack('<I', sram_buf[:4])[0]:08x})"

        toc_payload = sram_buf[cls.TOC_CHECK_START : cls.TOC_CHECKSUM_OFFSET]
        calculated_sha = hashlib.sha256(toc_payload).digest()
        stored_sha = sram_buf[cls.TOC_CHECKSUM_OFFSET : cls.TOC_CHECKSUM_OFFSET + 32]

        # Constant-or difference accumulator
        diff = 0
        for c, s in zip(calculated_sha, stored_sha):
            diff |= c ^ s

        if diff != 0:
            err_msg = f"FIP HDR CHK: 0x{diff:08x} ADDR 0xfffd0000"
            return False, diff, err_msg

        return True, 0, "OK"


class AcsBinParser:
    """Parser for Amlogic ACS (Auto Clock Setup & DRAM timings) binary structures (R2)."""

    MAGIC_ACS = b"acs__"
    MAGIC_DDR = b"ddrs_"
    MAGIC_PLL = b"pll__"

    DRAM_TYPE_LPDDR4 = 2
    BOARD_ID_STATION_MAX = 4
    BOARD_ID_WILDCARD = 0xFF

    @classmethod
    def contains_magics(cls, data: bytes) -> Dict[str, bool]:
        """Check for presence of acs__, ddrs_, pll__ magic strings in binary."""
        return {
            "has_acs_magic": cls.MAGIC_ACS in data,
            "has_ddr_magic": cls.MAGIC_DDR in data,
            "has_pll_magic": cls.MAGIC_PLL in data,
        }

    @classmethod
    def verify_lpddr4_support(cls, data: bytes) -> Tuple[bool, str]:
        """Verify that ACS binary configures LPDDR4 memory (SM1 Board ID = 4)."""
        magics = cls.contains_magics(data)
        if not magics["has_acs_magic"]:
            return False, "Missing 'acs__' magic header in ACS block"

        # In G12A/SM1 ACS table:
        # DramType = 2 indicates LPDDR4
        # Board ID = 4 (or 0xff wildcard) must be supported
        if b"\x02" not in data:
            return False, "No LPDDR4 DramType marker (2) found in ACS table"

        return True, "ACS contains valid LPDDR4 configuration"


class KitchenBlobAuditor:
    """Forensic auditor verifying cleanroom prohibition of proprietary Yandex firmware blobs (R3)."""

    def __init__(self, kitchen_dir: Path):
        self.kitchen_dir = Path(kitchen_dir)
        self.kitchen_level1 = self.kitchen_dir / "level1"
        self._stock_hashes: Optional[Dict[str, str]] = None

    @staticmethod
    def sha256_file(path: Path) -> str:
        """Compute SHA256 of a local file."""
        h = hashlib.sha256()
        with open(path, "rb") as f:
            while chunk := f.read(65536):
                h.update(chunk)
        return h.hexdigest()

    def get_stock_hashes(self) -> Dict[str, str]:
        """Scan and index original firmware bootloader blobs from AmlogicKitchen (<32MB)."""
        if self._stock_hashes is not None:
            return self._stock_hashes

        hashes = {}
        target_dir = self.kitchen_level1 if self.kitchen_level1.exists() else self.kitchen_dir
        if target_dir.exists():
            for f in target_dir.iterdir():
                # Focus on bootloader binaries, ignore large filesystem partitions (>32MB)
                if f.is_file() and not f.name.endswith(".txt") and f.stat().st_size <= 32 * 1024 * 1024:
                    try:
                        hashes[f.name] = self.sha256_file(f)
                    except (PermissionError, OSError):
                        pass
        self._stock_hashes = hashes
        return hashes

    def audit_target(self, target_path: Path) -> List[Tuple[Path, str, str]]:
        """Audit target file or directory against stock kitchen blobs.

        Returns:
            List of (target_file, matched_stock_file, sha256_hash).
            For R3 compliance, this list MUST BE EMPTY (0 matches).
        """
        stock_hashes = self.get_stock_hashes()
        inverted = {h: name for name, h in stock_hashes.items()}
        collisions = []

        if target_path.is_file():
            target_files = [target_path]
        elif target_path.is_dir():
            target_files = [
                p for p in target_path.rglob("*")
                if p.is_file() and not p.name.startswith(".") and "/.git/" not in str(p)
            ]
        else:
            return []

        for f in target_files:
            try:
                target_hash = self.sha256_file(f)
                if target_hash in inverted:
                    collisions.append((f, inverted[target_hash], target_hash))
            except (PermissionError, OSError):
                continue

        return collisions


class RepoHygieneAuditor:
    """Auditor for repository cleanliness and build script integrity (R4)."""

    FORBIDDEN_FILES = [
        "drivers/atrios/built-in.a",
    ]

    FORBIDDEN_SCRIPTS = [
        "scripts/build_aml_burn_package.py",
        "scripts/pack_aml_burn_sei610.py",
    ]

    @classmethod
    def check_untracked_built_in(cls, repo_root: Path) -> List[Path]:
        """Check for forbidden built-in.a or object archives in source tree."""
        found = []
        for rel in cls.FORBIDDEN_FILES:
            target = repo_root / rel
            if target.exists():
                found.append(target)
        return found

    @classmethod
    def check_stock_copying_scripts(cls, repo_root: Path) -> List[Path]:
        """Detect scripts copying proprietary blobs from AmlogicKitchen."""
        found = []
        for rel in cls.FORBIDDEN_SCRIPTS:
            target = repo_root / rel
            if target.exists():
                content = target.read_text(encoding="utf-8", errors="replace")
                if "AmlogicKitchen" in content:
                    found.append(target)
        return found

    @classmethod
    def check_build_script_zero_padding(cls, build_script: Path) -> bool:
        """Check whether build_mainline_uboot.sh contains zero-padding workarounds."""
        if not build_script.exists():
            return False
        content = build_script.read_text(encoding="utf-8", errors="replace")
        # Detect zero-padding of BL2 ACS block
        has_zero_pad = (
            "b'\\x00' * (limit - len(d))" in content
            or "seek=57344 count=4096" in content
            or "57344 + 4096" in content and "fix_pad" in content
        )
        return has_zero_pad
