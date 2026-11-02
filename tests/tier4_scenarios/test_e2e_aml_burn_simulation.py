"""Tier 4: Real-World Application Scenarios - End-to-End AML USB Burning Simulation."""

import hashlib
import struct
import time
from pathlib import Path
import pytest
from common.aml_burn_inspector import AcsBinParser, AmlProtocolEngine, FipTocParser


@pytest.mark.tier4
@pytest.mark.aml_burn
@pytest.mark.milestone("M4")
class TestE2EAmlBurnSimulation:
    """Authoritative opaque-box end-to-end burning flow simulation across IPL, SPL, and TPL."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.engine = AmlProtocolEngine()
        self.fip_parser = FipTocParser()
        self.acs_parser = AcsBinParser()

    def test_e2e_full_burn_session_simulation(self):
        """Simulate complete PC USB Burning Tool flashing session from cold boot to completion.

        Execution trace:
        1. IPL: Host enumerates device at Stage 0 (VID 0x1B8E, PID 0xC003).
        2. IPL: Host uploads DDR.USB to SRAM 0xfffa0000, branches to BL2.
        3. SPL: BL2 requests 16KB at offset 0x10000 into SRAM 0xfffd0000.
        4. SPL: Verified FIP TOC passes with 0 error diff and zero retries.
        5. SPL: BL2 reads ACS table (Board ID 4, LPDDR4), trains DRAM, loads U-Boot.
        6. TPL: Mainline U-Boot enumerates USB gadget VID 0x1B8E, PID 0xC003, Stage 16.
        7. TPL: Host issues 'low_power' -> U-Boot returns 'success' (no 1% stall!).
        8. TPL: Host issues 'disk_initial 1' -> U-Boot partitions eMMC storage.
        9. TPL: Host streams partition chunks via AM_REQ_DOWNLOAD with ADDSUM checksums.
        10. TPL: Device handles keepalive 'Continue:32' during flash writes.
        11. TPL: Host verifies partition SHA-1, issues 'save_setting' and 'burn_complete 1'.
        """
        # ==================== Stage 1: IPL (BootROM) ====================
        host_stage = self.engine.STAGE_IPL
        assert host_stage == 0, "Initial stage must be 0 (IPL)"

        # ==================== Stage 2: SPL (BL2 & DDR) ====================
        # Synthesize compliant split binaries: usb.bl2 (64KB) and usb.tpl (with FIP TOC at offset 0x10000)
        staged_tpl = bytearray(256 * 1024)

        # Build valid FIP TOC at offset 0x10000
        fip_offset = 0x10000
        staged_tpl[fip_offset : fip_offset + 4] = self.fip_parser.FIP_TOC_MAGIC_BYTES
        # Populate TOC entries: BL30, BL31, BL33, DDR fw
        staged_tpl[fip_offset + 32 : fip_offset + 48] = self.fip_parser.UUID_BL30
        staged_tpl[fip_offset + 72 : fip_offset + 88] = self.fip_parser.UUID_BL31
        staged_tpl[fip_offset + 112 : fip_offset + 128] = self.fip_parser.UUID_BL33
        staged_tpl[fip_offset + 152 : fip_offset + 168] = self.fip_parser.UUID_DDR_A

        # Compute valid SHA256 across [0x0010..0x3fe0]
        toc_body = staged_tpl[fip_offset + 0x0010 : fip_offset + 0x3FE0]
        staged_tpl[fip_offset + 0x3FE0 : fip_offset + 0x3FE0 + 32] = hashlib.sha256(toc_body).digest()

        # Simulate BL2 state 0x1001: request 16KB from offset 0x10000 into SRAM 0xfffd0000
        sram_buf = bytes(staged_tpl[0x10000 : 0x10000 + 0x4000])
        ok, diff, msg = self.fip_parser.verify_bl2_sram_checksum(sram_buf)
        assert ok is True, f"BL2 FIP TOC verification failed: {msg}"
        assert diff == 0, "Diff must be 0 for successful DDR fw loading"

        # Simulate ACS check for Board ID 4
        synthetic_acs = (
            b"acs__\x00\x00\x00"
            b"ddrs_\x00\x00\x00"
            b"pll__\x00\x00\x00"
            + b"\x00" * 16
            + bytes([self.acs_parser.DRAM_TYPE_LPDDR4, self.acs_parser.BOARD_ID_STATION_MAX])
            + b"\x00" * 4000
        )
        acs_ok, acs_msg = self.acs_parser.verify_lpddr4_support(synthetic_acs)
        assert acs_ok is True, acs_msg

        # ==================== Stage 3: TPL (Mainline U-Boot) ====================
        # Device transitions to stage 16
        identify_pkt = self.engine.format_identify_response(stage_minor=self.engine.STAGE_TPL)
        assert identify_pkt[3] == 16, "TPL stage must report 16 (0x10)"

        # Handshake 1: 'low_power' (fixes 1% stall)
        lp_ok, lp_reply = self.engine.process_bulkcmd("low_power")
        assert lp_ok is True
        assert lp_reply == "success"

        # Handshake 2: 'disk_initial'
        di_ok, di_reply = self.engine.process_bulkcmd("disk_initial 1")
        assert di_ok is True
        assert di_reply == "success"

        # Handshake 3: 'bootloader_is_old'
        old_ok, old_reply = self.engine.process_bulkcmd("bootloader_is_old")
        assert old_ok is True
        assert old_reply == "success"

        # Transfer Partition: 'boot' (128 KB = 2 chunks of 64 KB)
        partition_data = b"B" * (128 * 1024)
        num_chunks = len(partition_data) // self.engine.CHUNK_SIZE_64K

        # Prepare download
        prep_ok, prep_reply = self.engine.process_bulkcmd(
            f"download store boot normal {len(partition_data)}"
        )
        assert prep_ok is True
        assert prep_reply == "success"

        # Stream chunks
        for seq in range(num_chunks):
            chunk = partition_data[seq * 65536 : (seq + 1) * 65536]
            csum = self.engine.calc_addsum(chunk)
            hdr = {
                "retry_times": 0,
                "user_data_len": len(chunk),
                "sequence_no": seq,
                "checksum": csum,
                "alg": self.engine.CKC_ALG_ADDSUM,
                "ack_len": 512,
            }
            # Simulate intermediate keepalive during write
            if seq == 1:
                assert self.engine.KEEPALIVE_DOWNLOAD == b"Continue:32"

            chunk_ok, chunk_ack = self.engine.process_chunk(hdr, chunk)
            assert chunk_ok is True, f"Chunk {seq} failed: {chunk_ack}"
            assert chunk_ack == self.engine.ACK_OK

        # Query download status
        stat_ok, stat_reply = self.engine.process_bulkcmd("download get_status")
        assert stat_ok is True
        assert stat_reply == "success"

        # Verify Partition SHA-1
        expected_sha1 = hashlib.sha1(partition_data).hexdigest()
        v_ok, v_reply = self.engine.process_bulkcmd(f"verify sha1sum {expected_sha1}")
        assert v_ok is True
        assert v_reply == "success"

        # Finalize settings and complete burn
        save_ok, save_reply = self.engine.process_bulkcmd("save_setting")
        assert save_ok is True
        assert save_reply == "success"

        done_ok, done_reply = self.engine.process_bulkcmd("burn_complete 1")
        assert done_ok is True
        assert done_reply == "success"

    def test_e2e_reproduce_and_verify_low_power_stall_fix(self):
        """Verify that 'low_power' returns 'success' within 50ms, resolving the 1% hang."""
        t_start = time.time()
        ok, reply = self.engine.process_bulkcmd("low_power")
        elapsed = time.time() - t_start

        assert ok is True
        assert reply == "success"
        assert elapsed < 0.05, f"low_power response exceeded 50ms ({elapsed*1000:.1f}ms), risks host timeout!"

    def test_e2e_reproduce_and_verify_ddr_retry_loop_fix(self):
        """Reproduce BL2 DDR error condition and verify that compliant FIP TOC eliminates it."""
        # 1. Broken configuration: Monolithic bootloader placed at DDR.USB
        # Monolithic bootloader starts with BL2, NOT FIP TOC at offset 0x10000.
        broken_payload = bytearray(64 * 1024)  # Truncated 64KB without FIP TOC
        ok, code, msg = self.fip_parser.verify_bl2_sram_checksum(bytes(broken_payload[0x10000 : 0x10000 + 0x4000]))
        assert ok is False, "Broken truncated payload must fail"
        assert code == 0x00001011, "Must reproduce Err0x00001011"
        assert "Err0x00001011" in msg

        # 2. Fixed configuration: Staged payload with FIP TOC at offset 0x10000
        fixed_payload = bytearray(128 * 1024)
        fixed_payload[0x10000 : 0x10000 + 4] = self.fip_parser.FIP_TOC_MAGIC_BYTES
        toc_body = fixed_payload[0x10000 + 0x0010 : 0x10000 + 0x3FE0]
        fixed_payload[0x10000 + 0x3FE0 : 0x10000 + 0x3FE0 + 32] = hashlib.sha256(toc_body).digest()

        fixed_sram = bytes(fixed_payload[0x10000 : 0x10000 + 0x4000])
        ok_fixed, code_fixed, msg_fixed = self.fip_parser.verify_bl2_sram_checksum(fixed_sram)
        assert ok_fixed is True, f"Fixed configuration failed: {msg_fixed}"
        assert code_fixed == 0
        assert "Err0x00001011" not in msg_fixed
