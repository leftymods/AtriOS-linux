"""Tier 3: Cross-Feature Combinations - BL2, ACS Timing Table & FIP Microcode Staging."""

import hashlib
import struct
from pathlib import Path
import pytest
from common.aml_burn_inspector import AcsBinParser, FipTocParser


@pytest.mark.tier3
@pytest.mark.cross_feature
@pytest.mark.aml_burn
@pytest.mark.milestone("M1")
class TestCombBl2FipMicrocode:
    """Validate cross-feature integration between BL2, ACS timing tables, and FIP DDR microcode."""

    @pytest.fixture(autouse=True)
    def setup(self, fip_dir: Path, upgrade_dir: Path):
        self.fip_parser = FipTocParser()
        self.acs_parser = AcsBinParser()
        self.fip_dir = fip_dir
        self.upgrade_dir = upgrade_dir

    def test_comb_bl2_acs_stitching_structure(self, fip_dir: Path):
        """Cross-verify that BL2 binary is stitched with real ACS timing table at offset 57344."""
        bl2_candidates = [
            fip_dir / "fip" / "_tmp" / "bl2_new.bin",
            fip_dir / "fip" / "_tmp" / "bl2.n.bin.sig",
            fip_dir / "bl2" / "bin" / "g12a" / "bl2.bin",
        ]
        target_bl2 = None
        for cand in bl2_candidates:
            if cand.exists():
                target_bl2 = cand
                break

        if not target_bl2:
            pytest.skip("bl2 binary not found in uboot-repo")

        data = target_bl2.read_bytes()
        # If padded to 61440, offset 57344 must contain acs__
        if len(data) >= 61440:
            acs_block = data[57344:61440]
            magics = self.acs_parser.contains_magics(acs_block)
            assert magics["has_acs_magic"], (
                f"ACS block at offset 57344 in {target_bl2.name} is missing 'acs__' magic (zero-pad violation)!"
            )

    def test_comb_fip_toc_at_offset_0x10000(self, upgrade_dir: Path):
        """Cross-verify that staged bootloader payload contains valid FIP TOC at offset 0x10000."""
        bootloader_img = upgrade_dir / "bootloader.img"
        if not bootloader_img.exists():
            pytest.skip("bootloader.img not yet built by M1/M3")

        data = bootloader_img.read_bytes()
        assert len(data) > 0x10000 + 0x4000, f"bootloader.img size {len(data)} too small for FIP TOC"

        # Extract 16KB loaded into SRAM 0xfffd0000 by BL2 in USB mode
        sram_buf = data[0x10000 : 0x10000 + 0x4000]

        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(sram_buf)
        assert ok is True, f"FIP TOC verification at offset 0x10000 failed: {msg}"
        assert diff == 0, f"Accumulated XOR mismatch {diff:08x} != 0"

    def test_comb_bl2_lpddr4_microcode_handoff_simulation(self):
        """Simulate BL2 USB boot handoff: ACS Board ID 4 detection -> FIP TOC check -> DDR microcode."""
        # 1. Synthesize combined 128KB bootloader binary
        payload = bytearray(128 * 1024)

        # Place ACS block in BL2 section
        payload[57344 : 57344 + 5] = b"acs__"
        payload[57344 + 5 : 57344 + 10] = b"ddrs_"
        payload[57344 + 10 : 57344 + 15] = b"pll__"
        # Set DramType = 2 (LPDDR4) for Board ID = 4
        payload[57344 + 32] = 2  # DramType LPDDR4
        payload[57344 + 33] = 4  # Board ID 4

        # Place valid FIP TOC at offset 0x10000 (64KB)
        fip_start = 0x10000
        payload[fip_start : fip_start + 4] = self.fip_parser.FIP_TOC_MAGIC_BYTES
        # Put DDR microcode UUID into TOC entry
        entry_offset = fip_start + 32
        payload[entry_offset : entry_offset + 16] = self.fip_parser.UUID_DDR_A
        # Calculate valid SHA256 over [0x0010..0x3fe0]
        toc_payload = payload[fip_start + 0x0010 : fip_start + 0x3FE0]
        h = hashlib.sha256(toc_payload).digest()
        payload[fip_start + 0x3FE0 : fip_start + 0x3FE0 + 32] = h

        # 2. Simulate BL2 USB read of 16KB from offset 0x10000 into SRAM 0xfffd0000
        sram_slice = bytes(payload[0x10000 : 0x10000 + 0x4000])

        # Verify ACS timing table
        acs_slice = bytes(payload[57344:61440])
        magics = self.acs_parser.contains_magics(acs_slice)
        assert magics["has_acs_magic"] is True
        assert magics["has_ddr_magic"] is True
        assert self.acs_parser.verify_lpddr4_support(acs_slice)[0] is True

        # Verify FIP TOC
        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(sram_slice)
        assert ok is True, f"FIP TOC failed: {msg}"
        assert diff == 0
        assert "Err0x00001011" not in msg
