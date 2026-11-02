"""Milestone M1 Empirical Adversarial Challenge Test Suite.

Authoritative stress tests, corruption harnesses, and boundary verification
challenging the BL2 DDR microcode, ACS stitching, and USB binary splitting implementation.
"""

import hashlib
import os
import random
import struct
from pathlib import Path
import pytest

from common.aml_burn_inspector import AcsBinParser, FipTocParser


@pytest.mark.tier2
@pytest.mark.requirement("R2")
@pytest.mark.aml_burn
@pytest.mark.milestone("M1")
class TestM1AdversarialChallenge:
    """Adversarial challenge test suite for Milestone M1."""

    @pytest.fixture(autouse=True)
    def setup(self, repo_root: Path):
        self.repo_root = repo_root
        self.staging_dir = repo_root / "output" / "burn_staging"
        self.upgrade_dir = repo_root / "device" / "yandex" / "station_max" / "upgrade"
        self.fip_tmp = repo_root / "bootloader" / "uboot-repo" / "fip" / "_tmp"
        self.fip_g12a = repo_root / "bootloader" / "uboot-repo" / "fip" / "g12a"

        self.ddr_usb_path = self.staging_dir / "DDR.USB"
        self.uboot_usb_path = self.staging_dir / "UBOOT.USB"
        self.bootloader_img_path = self.upgrade_dir / "bootloader.img"
        self.bl2_usb_path = self.upgrade_dir / "u-boot.bin.usb.bl2"
        self.tpl_usb_path = self.upgrade_dir / "u-boot.bin.usb.tpl"
        self.sd_bin_path = self.upgrade_dir / "u-boot.bin.sd.bin"
        self.sd_uboot_path = self.staging_dir / "aml_sdc_burn.UBOOT"
        self.bl2_new_path = self.fip_tmp / "bl2_new.bin"
        self.bl2_sig_path = self.fip_tmp / "bl2.n.bin.sig"
        self.acs_bin_path = self.fip_g12a / "acs.bin"

        self.fip_parser = FipTocParser()
        self.acs_parser = AcsBinParser()

    # =========================================================================
    # 1. Byte-by-byte Binary Identity and Splitting Integrity
    # =========================================================================

    def test_challenge_split_binaries_concatenation_equals_bootloader(self):
        """Verify u-boot.bin.usb.bl2 + u-boot.bin.usb.tpl exactly equals bootloader.img."""
        assert self.bl2_usb_path.exists(), "u-boot.bin.usb.bl2 missing"
        assert self.tpl_usb_path.exists(), "u-boot.bin.usb.tpl missing"
        assert self.bootloader_img_path.exists(), "bootloader.img missing"

        bl2_data = self.bl2_usb_path.read_bytes()
        tpl_data = self.tpl_usb_path.read_bytes()
        bootloader_data = self.bootloader_img_path.read_bytes()

        assert len(bl2_data) == 65536, f"u-boot.bin.usb.bl2 size {len(bl2_data)} != 65536"
        assert len(tpl_data) == len(bootloader_data) - 65536, "TPL size must equal bootloader.img minus 64KB BL2"
        assert len(bootloader_data) > 0x14000, "bootloader.img too small"

        assert bl2_data + tpl_data == bootloader_data, (
            "Split binaries concatenation (BL2 + TPL) does NOT match bootloader.img byte-for-byte!"
        )

    def test_challenge_staging_binary_identities(self):
        """Verify staged binaries match their respective source artifacts byte-for-byte."""
        assert self.ddr_usb_path.exists()
        assert self.uboot_usb_path.exists()
        assert self.sd_uboot_path.exists()

        ddr_data = self.ddr_usb_path.read_bytes()
        bootloader_data = self.bootloader_img_path.read_bytes()
        assert ddr_data == bootloader_data, "DDR.USB != bootloader.img"

        uboot_data = self.uboot_usb_path.read_bytes()
        tpl_data = self.tpl_usb_path.read_bytes()
        assert uboot_data == tpl_data, "UBOOT.USB != u-boot.bin.usb.tpl"

        sd_uboot_data = self.sd_uboot_path.read_bytes()
        sd_bin_data = self.sd_bin_path.read_bytes()
        assert sd_uboot_data == sd_bin_data, "aml_sdc_burn.UBOOT != u-boot.bin.sd.bin"
        assert sd_bin_data[512:] == bootloader_data, "SD binary payload after 512-byte header != bootloader.img"

    # =========================================================================
    # 2. FIP TOC and SHA256 Verification at Offset 0x10000
    # =========================================================================

    def test_challenge_fip_toc_at_offset_0x10000_exact_match(self):
        """Verify DDR.USB at offset 0x10000 (length 16KB) contains valid FIP TOC and matches SHA256."""
        ddr_data = self.ddr_usb_path.read_bytes()
        assert len(ddr_data) >= 0x10000 + 0x4000, "DDR.USB too small for 16KB FIP TOC"

        sram_slice = ddr_data[0x10000 : 0x10000 + 0x4000]
        assert len(sram_slice) == 0x4000

        # Offset 0x00..0x10 in sram_slice (0x10000..0x10010) is padding
        assert sram_slice[:16] == b"\x00" * 16, "SRAM slice [0x00:0x10] must be zero padding"

        # Magic at offset 0x10
        magic = struct.unpack("<I", sram_slice[16:20])[0]
        assert magic == 0xAA640001, f"Invalid FIP magic 0x{magic:08x} at offset 0x10010"

        # SHA256 verification over [0x0010..0x3fe0]
        payload = sram_slice[0x0010:0x3FE0]
        stored_hash = sram_slice[0x3FE0 : 0x3FE0 + 32]
        computed_hash = hashlib.sha256(payload).digest()
        assert stored_hash == computed_hash, "SHA256 mismatch between payload and TOC header"

        # Verify via parser
        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(sram_slice)
        assert ok is True, f"FIP checksum verification failed: {msg}"
        assert diff == 0

        # Parse TOC entries and ensure key images exist
        entries = self.fip_parser.parse_toc(sram_slice[0x10:])
        assert len(entries) >= 4, f"Expected at least 4 TOC entries, got {len(entries)}"
        entry_uuids = [e["uuid"] for e in entries]
        assert self.fip_parser.UUID_BL30 in entry_uuids, "BL30 entry missing from TOC"
        assert self.fip_parser.UUID_BL31 in entry_uuids, "BL31 entry missing from TOC"
        assert self.fip_parser.UUID_BL33 in entry_uuids, "BL33 entry missing from TOC"

    # =========================================================================
    # 3. Adversarial Random Offset Access Fuzzing on DDR.USB
    # =========================================================================

    def test_challenge_ddr_usb_random_offset_access_fuzzing(self):
        """Adversarial stress test: accessing DDR.USB at random offsets must NEVER pass FIP verification."""
        ddr_data = self.ddr_usb_path.read_bytes()
        file_len = len(ddr_data)

        # Offsets to test: boundary cases + 500 pseudo-random offsets
        deterministic_offsets = [
            0,
            1,
            2,
            3,
            4,
            16,
            512,
            4096,
            16384,
            32768,
            57344,
            57776,
            61440,
            61872,
            65535,  # 0x10000 - 1
            65537,  # 0x10000 + 1
            65552,  # 0x10010 (misaligned by 16 bytes: magic at offset 0)
            70000,
            81920,  # 0x14000
            131072,  # 128KB
            file_len - 16384,
            file_len - 100,
        ]

        rng = random.Random(0xDEADBEEF)
        random_offsets = [rng.randint(0, file_len - 1) for _ in range(500)]
        all_offsets = sorted(set(deterministic_offsets + random_offsets))

        passed_offsets = []
        for offset in all_offsets:
            if offset == 0x10000:
                continue
            slice_data = ddr_data[offset : offset + 0x4000]
            ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(slice_data)
            if ok:
                passed_offsets.append(offset)

        assert not passed_offsets, (
            f"Adversarial vulnerability! Random offsets unexpectedly passed FIP verification: {passed_offsets}"
        )

    def test_challenge_ddr_usb_misaligned_0x10010_fails_checksum(self):
        """Verify that slicing DDR.USB at 0x10010 (where magic is at 0) fails SHA256 verification."""
        ddr_data = self.ddr_usb_path.read_bytes()
        misaligned_slice = ddr_data[0x10010 : 0x10010 + 0x4000]

        # Magic check passes because magic is at index 0 of slice
        assert self.fip_parser.check_magic(misaligned_slice) is True

        # BUT BL2 checksum verification MUST fail with non-zero diff
        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(misaligned_slice)
        assert ok is False, "Misaligned offset 0x10010 must not pass BL2 checksum verification"
        assert diff != 0, f"Expected non-zero diff, got {diff}"
        assert "FIP HDR CHK" in msg

    # =========================================================================
    # 4. ACS Signature and Board ID / DramType Validation
    # =========================================================================

    def test_challenge_acs_signature_offsets_in_bl2_new_and_bootloader(self):
        """Verify acs__ signature in bl2_new.bin (offset 57776) and bootloader.img (offset 61872).

        Logic Chain:
        - In bl2_new.bin: bl2.bin is padded to 57,344 bytes. acs.bin has acs__ at offset 432.
          Therefore acs__ is at 57344 + 432 = 57,776.
        - In bootloader.img / bl2.n.bin.sig: aml_encrypt prepends a 4,096-byte header (@AML...).
          Therefore acs__ shifts to 57776 + 4096 = 61,872 (0xF1B0).
        - At offset 57,776 in bootloader.img, raw BL2 code is located (NOT acs__).
        """
        assert self.bl2_new_path.exists(), "bl2_new.bin missing"
        bl2_new_data = self.bl2_new_path.read_bytes()
        assert len(bl2_new_data) == 61440, f"bl2_new.bin size {len(bl2_new_data)} != 61440"

        # 1. In bl2_new.bin: signature MUST be at 57,776
        assert bl2_new_data[57776 : 57776 + 5] == b"acs__", (
            f"bl2_new.bin missing acs__ at 57776: got {bl2_new_data[57776:57776+5]}"
        )
        assert bl2_new_data.find(b"acs__") == 57776

        # 2. In bootloader.img: verify offset 57,776 does NOT contain acs__ due to 4KB AML header
        bootloader_data = self.bootloader_img_path.read_bytes()
        assert bootloader_data[57776 : 57776 + 5] != b"acs__", (
            "bootloader.img unexpectedly contains acs__ at 57776 (header missing?)"
        )

        # 3. In bootloader.img: signature MUST be at 61,872 (57776 + 4096)
        expected_sig_offset = 57776 + 4096  # 61872 = 0xF1B0
        assert bootloader_data[expected_sig_offset : expected_sig_offset + 5] == b"acs__", (
            f"bootloader.img missing acs__ at {expected_sig_offset}: got {bootloader_data[expected_sig_offset:expected_sig_offset+5]}"
        )
        assert bootloader_data.find(b"acs__") == expected_sig_offset

        # 4. In u-boot.bin.usb.bl2: signature MUST also be at 61,872
        bl2_usb_data = self.bl2_usb_path.read_bytes()
        assert bl2_usb_data.find(b"acs__") == expected_sig_offset

        # 5. In acs.bin: entry point pointer at offset 4 matches 0xF1B0 (61872)
        acs_data = self.acs_bin_path.read_bytes()
        ep = struct.unpack("<H", acs_data[4:6])[0]
        assert ep == 61872, f"acs.bin entry point pointer {ep} != 61872"

    def test_challenge_acs_board_id_4_and_dramtype_2_lpddr4(self):
        """Verify ACS table specifies DramType = 2 (LPDDR4) and supports Board ID 4 / 0xFF."""
        acs_data = self.acs_bin_path.read_bytes()
        assert len(acs_data) == 1488, f"acs.bin size {len(acs_data)} != 1488"

        # Verify magics
        magics = self.acs_parser.contains_magics(acs_data)
        assert magics["has_acs_magic"], "Missing acs__ magic in acs.bin"
        assert magics["has_ddr_magic"], "Missing ddrs_ magic in acs.bin"
        assert magics["has_pll_magic"], "Missing pll__ magic in acs.bin"

        # Verify DramType = 2 (LPDDR4) in all cfg. entries
        import re
        cfg_matches = list(re.finditer(b"cfg\\.", acs_data))
        assert len(cfg_matches) == 4, f"Expected 4 cfg. entries in acs.bin, found {len(cfg_matches)}"

        for idx, match in enumerate(cfg_matches):
            pos = match.start()
            entry = acs_data[pos : pos + 32]
            board_mask = entry[12]
            dram_type = entry[14]
            # board_mask must be 0xFF (wildcard matching Board ID 4)
            assert board_mask == 0xFF, f"cfg[{idx}] board mask 0x{board_mask:02x} != 0xFF"
            # dram_type must be 2 (LPDDR4)
            assert dram_type == 2, f"cfg[{idx}] dram_type {dram_type} != 2 (LPDDR4)"

        ok, msg = self.acs_parser.verify_lpddr4_support(acs_data)
        assert ok is True, f"ACS LPDDR4 verification failed: {msg}"

    # =========================================================================
    # 5. Truncation, Underrun, and Bit-Flip Corruption Stress Testing
    # =========================================================================

    def test_challenge_ddr_usb_truncation_underrun_rejection(self):
        """Verify truncated DDR.USB (< 81,920 bytes) triggers Err0x00001011."""
        full_ddr = self.ddr_usb_path.read_bytes()

        # Case A: 64KB file (the original bug where DDR.USB was only BL2)
        truncated_64k = full_ddr[:65536]
        # BL2 attempting to read 16KB from offset 0x10000 gets 0 bytes (EOF)
        read_0x10000 = truncated_64k[0x10000 : 0x10000 + 0x4000]
        assert len(read_0x10000) == 0
        ok, code, msg = self.fip_parser.verify_bl2_sram_checksum(read_0x10000)
        assert ok is False
        assert code == 0x00001011
        assert "Err0x00001011" in msg

        # Case B: Partial read (4KB available at offset 0x10000)
        truncated_68k = full_ddr[: 65536 + 4096]
        partial_read = truncated_68k[0x10000 : 0x10000 + 0x4000]
        assert len(partial_read) == 4096
        ok, code, msg = self.fip_parser.verify_bl2_sram_checksum(partial_read)
        assert ok is False
        assert code == 0x00001011

    def test_challenge_fip_toc_single_bit_flip_adversarial_detection(self):
        """Verify that single-bit corruptions across FIP TOC payload are 100% detected."""
        ddr_data = self.ddr_usb_path.read_bytes()
        valid_toc = bytearray(ddr_data[0x10000 : 0x10000 + 0x4000])

        rng = random.Random(0x42)
        # Test 100 random bit flips in payload [0x0010..0x3fe0]
        payload_start = 0x0010
        payload_end = 0x3FE0

        for _ in range(100):
            corrupted = bytearray(valid_toc)
            byte_idx = rng.randint(payload_start, payload_end - 1)
            bit_idx = rng.randint(0, 7)
            corrupted[byte_idx] ^= 1 << bit_idx

            ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(bytes(corrupted))
            assert ok is False, f"Bit flip at byte {byte_idx} bit {bit_idx} was NOT detected!"
            assert diff != 0
            assert "FIP HDR CHK" in msg

    def test_challenge_fip_magic_corruption_rejection(self):
        """Verify corrupting FIP TOC magic triggers Bad Magic error."""
        ddr_data = self.ddr_usb_path.read_bytes()
        valid_toc = bytearray(ddr_data[0x10000 : 0x10000 + 0x4000])

        # Corrupt magic at 0x10..0x14
        corrupted = bytearray(valid_toc)
        corrupted[16:20] = b"\x00\x00\x00\x00"

        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(bytes(corrupted))
        assert ok is False
        assert diff == 0x000000F1
        assert "Bad Magic 0x00000000" in msg
