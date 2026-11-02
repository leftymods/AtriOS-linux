"""Tier 2: Boundary & Corner Cases - Requirement R2: BL2 DDR Microcode & FIP Integrity."""

import hashlib
import struct
import pytest
from common.aml_burn_inspector import AcsBinParser, FipTocParser


@pytest.mark.tier2
@pytest.mark.requirement("R2")
@pytest.mark.aml_burn
@pytest.mark.milestone("M1")
class TestR2Bl2DdrBoundaries:
    """Boundary and corruption stress tests for BL2 DDR microcode loading and FIP verification (R2)."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.fip_parser = FipTocParser()
        self.acs_parser = AcsBinParser()

    def test_r2_boundary_corrupted_fip_magic_fails_verification(self):
        """Verify non-matching FIP magic triggers FIP HDR CHK failure with exact signature."""
        block = bytearray(0x4000)
        # Corrupt magic
        block[0:4] = b"\xde\xad\xbe\xef"
        # Store dummy hash
        block[0x3FE0 : 0x3FE0 + 32] = b"\xaa" * 32

        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(bytes(block))
        assert ok is False, "Corrupted FIP magic must fail"
        assert "FIP HDR CHK" in msg, f"Expected FIP HDR CHK error, got: {msg}"
        assert "0xfffd0000" in msg

    def test_r2_boundary_single_bit_flip_in_checksum_produces_nonzero_diff(self):
        """Verify that a single-bit error in SHA256 produces non-zero XOR mismatch accumulator."""
        block = bytearray(0x4000)
        block[0:4] = self.fip_parser.FIP_TOC_MAGIC_BYTES
        valid_sha = hashlib.sha256(block[0x0010:0x3FE0]).digest()

        # Flip lowest bit of first byte of hash
        corrupted_sha = bytearray(valid_sha)
        corrupted_sha[0] ^= 0x01
        block[0x3FE0 : 0x3FE0 + 32] = bytes(corrupted_sha)

        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(bytes(block))
        assert ok is False
        assert diff == 0x01, f"Expected XOR difference 0x01, got 0x{diff:08x}"
        assert "FIP HDR CHK: 0x00000001 ADDR 0xfffd0000" in msg

    def test_r2_boundary_buffer_underrun_triggers_err0x00001011(self):
        """Verify truncated SRAM buffer (< 16KB) triggers verbatim Err0x00001011."""
        short_buf = b"\x01\x00\x64\xaa" + b"\x00" * 4092  # Only 4KB instead of 16KB
        ok, code, msg = self.fip_parser.verify_bl2_sram_checksum(short_buf)
        assert ok is False
        assert code == 0x00001011, f"Expected error code 0x00001011, got 0x{code:08x}"
        assert "Err0x00001011" in msg

    def test_r2_boundary_acs_missing_magic_rejected(self):
        """Verify ACS block without 'acs__' magic is rejected."""
        corrupted_acs = b"\x00" * 4096
        magics = self.acs_parser.contains_magics(corrupted_acs)
        assert magics["has_acs_magic"] is False
        assert magics["has_ddr_magic"] is False

        ok, msg = self.acs_parser.verify_lpddr4_support(corrupted_acs)
        assert ok is False
        assert "Missing 'acs__' magic" in msg

    def test_r2_boundary_acs_ddr3_rejected_for_station_max(self):
        """Verify ACS table configuring only DDR3 / DDR4 without LPDDR4 is rejected."""
        # acs block with only DramType = 0 (DDR3) or 1 (DDR4)
        mock_ddr3_acs = b"acs__" + b"\x00\x00\x00\x00" * 10
        ok, msg = self.acs_parser.verify_lpddr4_support(mock_ddr3_acs)
        assert ok is False
        assert "No LPDDR4 DramType marker" in msg

    def test_r2_boundary_fip_empty_or_sub_header_data(self):
        """Verify check_magic handles empty or sub-4-byte buffers without IndexError."""
        assert self.fip_parser.check_magic(b"") is False
        assert self.fip_parser.check_magic(b"\x01") is False
        assert self.fip_parser.check_magic(b"\x01\x00") is False
        assert self.fip_parser.check_magic(b"\x01\x00\x64") is False
