"""Tier 1: Feature Coverage - Requirement R1: AML Burn Protocol in Mainline U-Boot v2025.01."""

import os
import struct
from pathlib import Path
import pytest
from common.aml_burn_inspector import AmlProtocolEngine


@pytest.mark.tier1
@pytest.mark.requirement("R1")
@pytest.mark.aml_burn
@pytest.mark.milestone("M2")
class TestR1AmlBurnProtocol:
    """Validate Amlogic USB Burning Tool protocol compliance in Mainline U-Boot (v2025.01)."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.engine = AmlProtocolEngine()

    def test_r1_usb_identifiers_contract(self):
        """Verify standard USB VID, PID, and device class descriptors for AML burn mode."""
        assert self.engine.USB_VENDOR_ID == 0x1B8E, f"VID 0x{self.engine.USB_VENDOR_ID:04x} != 0x1b8e"
        assert self.engine.USB_PRODUCT_ID_BURNING == 0xC003, f"PID 0x{self.engine.USB_PRODUCT_ID_BURNING:04x} != 0xc003"
        assert self.engine.USB_BCD_DEVICE == 0x0007, f"bcdDevice 0x{self.engine.USB_BCD_DEVICE:04x} != 0x0007"
        assert self.engine.USB_CLASS_VENDOR_SPEC == 0xFF, f"Class 0x{self.engine.USB_CLASS_VENDOR_SPEC:02x} != 0xff"

    def test_r1_bulk_endpoints_mps_512(self):
        """Verify Bulk EP1 IN and EP2 OUT endpoints operate with MPS=512 for High Speed."""
        assert self.engine.USB_EP1_IN_ADDR == 0x81, "EP1 IN address must be 0x81"
        assert self.engine.USB_EP2_OUT_ADDR == 0x02, "EP2 OUT address must be 0x02"
        assert self.engine.USB_EP_MPS_HIGH_SPEED == 512, "High Speed Bulk MPS must be 512"
        assert self.engine.USB_EP0_MPS == 64, "EP0 MPS must be 64"

    def test_r1_identify_host_stage16_tpl(self):
        """Verify AM_REQ_IDENTIFY_HOST (0x20) response signals Stage 16 (Mainline U-Boot TPL)."""
        resp = self.engine.format_identify_response(stage_minor=self.engine.STAGE_TPL)
        assert len(resp) == 4, f"Identify response length {len(resp)} != 4"
        assert resp[0] == 0, "ver_major must be 0"
        assert resp[3] == 16, f"stage_minor must be 16 (0x10), got {resp[3]}"

    def test_r1_low_power_handshake_success(self):
        """Verify 'low_power' handshake returns 'success' to prevent 1% tool stall."""
        ok, reply = self.engine.process_bulkcmd("low_power")
        assert ok is True, "low_power command failed"
        assert reply == "success", f"low_power reply '{reply}' != 'success'"

    def test_r1_disk_initial_handshake_success(self):
        """Verify 'disk_initial' command initializes eMMC storage and returns 'success'."""
        ok, reply = self.engine.process_bulkcmd("disk_initial 1")
        assert ok is True, "disk_initial command failed"
        assert reply == "success", f"disk_initial reply '{reply}' != 'success'"

    def test_r1_write_media_header_packing(self):
        """Verify CmdData_WriteMedia 32-byte descriptor packing and unpacking."""
        test_len = 65536
        test_seq = 42
        test_csum = 0x12345678
        packed = self.engine.pack_write_media_header(
            user_data_len=test_len,
            sequence_no=test_seq,
            data_checksum=test_csum,
            retry_times=0,
            alg=self.engine.CKC_ALG_ADDSUM,
        )
        assert len(packed) == 32, f"Packed header size {len(packed)} != 32"

        unpacked = self.engine.unpack_write_media_header(packed)
        assert unpacked["user_data_len"] == test_len
        assert unpacked["sequence_no"] == test_seq
        assert unpacked["checksum"] == test_csum
        assert unpacked["alg"] == self.engine.CKC_ALG_ADDSUM

    def test_r1_addsum_checksum_calculation(self):
        """Verify 32-bit unsigned addition checksum (ADDSUM 0x00EF)."""
        # 16 bytes of sequential integers
        data = struct.pack("<4I", 1, 2, 3, 4)
        csum = self.engine.calc_addsum(data)
        assert csum == 10, f"Expected checksum 10, got {csum}"

        # Test with trailing remainder (e.g. 7 bytes)
        data7 = b"\x01\x00\x00\x00\x02\x00\x00"  # word1 = 1, word2 tail = 2
        csum7 = self.engine.calc_addsum(data7)
        assert csum7 == 3, f"Expected checksum 3, got {csum7}"

    def test_r1_download_chunk_ack_ok(self):
        """Verify 64KB media chunk with valid checksum produces 'OK!!' response."""
        chunk_data = b"A" * 65536
        csum = self.engine.calc_addsum(chunk_data)
        hdr = {
            "retry_times": 0,
            "user_data_len": len(chunk_data),
            "sequence_no": 0,
            "checksum": csum,
            "alg": self.engine.CKC_ALG_ADDSUM,
            "ack_len": 512,
        }
        ok, reply = self.engine.process_chunk(hdr, chunk_data)
        assert ok is True, f"process_chunk failed: {reply}"
        assert reply == self.engine.ACK_OK, f"ACK '{reply}' != 'OK!!'"

    def test_r1_keepalive_signals(self):
        """Verify keepalive constants 'Continue:32' and 'Continue:34' for watchdog reset."""
        assert self.engine.KEEPALIVE_DOWNLOAD == b"Continue:32"
        assert self.engine.KEEPALIVE_BULKCMD == b"Continue:34"

    def test_r1_uboot_defconfig_aml_burn_contract(self, uboot_defconfig: Path):
        """Verify uboot_station_max_defconfig specifies required USB gadget options."""
        assert uboot_defconfig.exists(), f"Defconfig missing at {uboot_defconfig}"
        content = uboot_defconfig.read_text(encoding="utf-8", errors="replace")

        # VID 0x1B8E
        assert "CONFIG_USB_GADGET_VENDOR_NUM=0x1b8e" in content or "0x1B8E" in content, (
            "CONFIG_USB_GADGET_VENDOR_NUM must be 0x1b8e"
        )
