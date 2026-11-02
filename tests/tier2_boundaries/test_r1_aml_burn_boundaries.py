"""Tier 2: Boundary & Corner Cases - Requirement R1: AML Burn Protocol."""

import struct
import pytest
from common.aml_burn_inspector import AmlProtocolEngine


@pytest.mark.tier2
@pytest.mark.requirement("R1")
@pytest.mark.aml_burn
@pytest.mark.milestone("M2")
class TestR1AmlBurnBoundaries:
    """Boundary, edge condition, and corrupted packet stress tests for AML burn protocol (R1)."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.engine = AmlProtocolEngine()

    def test_r1_boundary_checksum_mismatch_exact_error_format(self):
        """Verify exact error string format returned upon ADDSUM checksum discrepancy."""
        payload = b"\x01\x02\x03\x04"
        actual_csum = self.engine.calc_addsum(payload)
        corrupted_csum = actual_csum ^ 0xDEADBEEF

        hdr = {
            "retry_times": 1,
            "user_data_len": len(payload),
            "sequence_no": 5,
            "checksum": corrupted_csum,
            "alg": self.engine.CKC_ALG_ADDSUM,
            "ack_len": 512,
        }
        ok, reply = self.engine.process_chunk(hdr, payload)
        assert ok is False, "Corrupted checksum must not return success"
        reply_str = reply.decode("utf-8")
        assert "Alg[ef]0x:" in reply_str, f"Reply missing 'Alg[ef]0x:' prefix: {reply_str}"
        assert f"0x{actual_csum:08x}" in reply_str, f"Missing generated sum in: {reply_str}"
        assert f"0x{corrupted_csum:08x}" in reply_str, f"Missing expected sum in: {reply_str}"

    def test_r1_boundary_zero_length_payload(self):
        """Verify handling of zero-length user data payload."""
        payload = b""
        csum = self.engine.calc_addsum(payload)
        assert csum == 0, f"Empty payload checksum must be 0, got {csum}"

        hdr = {
            "retry_times": 0,
            "user_data_len": 0,
            "sequence_no": 0,
            "checksum": 0,
            "alg": self.engine.CKC_ALG_ADDSUM,
            "ack_len": 512,
        }
        ok, reply = self.engine.process_chunk(hdr, payload)
        assert ok is True, f"Zero-length chunk failed: {reply}"

    def test_r1_boundary_single_byte_payload(self):
        """Verify checksum and processing of 1-byte payload (sub-word boundary)."""
        payload = b"\x7F"
        csum = self.engine.calc_addsum(payload)
        assert csum == 0x7F, f"Expected 0x7F, got 0x{csum:x}"

        hdr = {
            "retry_times": 0,
            "user_data_len": 1,
            "sequence_no": 1,
            "checksum": csum,
            "alg": self.engine.CKC_ALG_ADDSUM,
            "ack_len": 512,
        }
        ok, reply = self.engine.process_chunk(hdr, payload)
        assert ok is True, f"Single-byte chunk failed: {reply}"

    def test_r1_boundary_mps_512_aligned_payload(self):
        """Verify chunk exactly matching USB High Speed endpoint MPS (512 bytes)."""
        payload = bytes([i % 256 for i in range(512)])
        csum = self.engine.calc_addsum(payload)
        hdr = {
            "retry_times": 0,
            "user_data_len": 512,
            "sequence_no": 2,
            "checksum": csum,
            "alg": self.engine.CKC_ALG_ADDSUM,
            "ack_len": 512,
        }
        ok, reply = self.engine.process_chunk(hdr, payload)
        assert ok is True
        assert reply == self.engine.ACK_OK

    def test_r1_boundary_payload_size_mismatch_rejection(self):
        """Verify rejection when payload length does not match header user_data_len."""
        payload = b"\x00" * 100
        hdr = {
            "retry_times": 0,
            "user_data_len": 200,  # Header claims 200, actual is 100
            "sequence_no": 3,
            "checksum": 0,
            "alg": self.engine.CKC_ALG_NONE,
            "ack_len": 512,
        }
        ok, reply = self.engine.process_chunk(hdr, payload)
        assert ok is False, "Mismatched payload length must be rejected"
        assert b"Length mismatch" in reply

    def test_r1_boundary_unknown_checksum_algorithm(self):
        """Verify rejection of unknown checksum algorithm selector (e.g. 0x9999)."""
        payload = b"\x01\x02\x03\x04"
        hdr = {
            "retry_times": 0,
            "user_data_len": 4,
            "sequence_no": 4,
            "checksum": 0,
            "alg": 0x9999,
            "ack_len": 512,
        }
        ok, reply = self.engine.process_chunk(hdr, payload)
        assert ok is False
        assert b"Unsupported checksum" in reply

    def test_r1_boundary_command_leading_trailing_whitespace(self):
        """Verify command parser is resilient to leading and trailing whitespace."""
        ok, reply = self.engine.process_bulkcmd("   low_power   \r\n")
        assert ok is True
        assert reply == "success"

    def test_r1_boundary_empty_and_whitespace_only_commands(self):
        """Verify empty and whitespace-only command strings return failure gracefully."""
        ok, reply = self.engine.process_bulkcmd("")
        assert ok is False
        assert "failed" in reply

        ok, reply = self.engine.process_bulkcmd("    \t\r\n")
        assert ok is False
        assert "failed" in reply

    def test_r1_boundary_truncated_write_media_header(self):
        """Verify header unpack raises ValueError on truncated header (< 32 bytes)."""
        with pytest.raises(ValueError, match="< 32 bytes"):
            self.engine.unpack_write_media_header(b"\x00" * 31)

    def test_r1_boundary_addsum_32bit_overflow(self):
        """Verify 32-bit addition arithmetic wraps around cleanly on overflow."""
        # Sum of two 0xFFFFFFFF values should wrap to 0xFFFFFFFE in 32-bit
        data = struct.pack("<2I", 0xFFFFFFFF, 0xFFFFFFFF)
        csum = self.engine.calc_addsum(data)
        assert csum == 0xFFFFFFFE, f"Expected 0xFFFFFFFE, got 0x{csum:08x}"
