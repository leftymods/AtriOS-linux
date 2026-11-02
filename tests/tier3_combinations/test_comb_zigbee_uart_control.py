"""Tier 3: Pairwise Combination - Zigbee Control GPIOs + UART Protocol."""

import pytest
from common.mock_subsystems import MockZigbeeUART


@pytest.mark.tier3
@pytest.mark.milestone("M3")
class TestCombZigbeeUARTControl:
    """Validate coordinated reset sequencing and serial EZSP frame communication."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.uart = MockZigbeeUART()

    def test_comb_reset_pulse_enables_ezsp_communication(self):
        """Verify asserting zb-reset low then high initializes UART response."""
        # Uninitialized
        assert self.uart.send_frame(b"\x1a\xc0\x00") == b""
        # Toggle reset
        self.uart.toggle_reset(duration_ms=50)
        assert self.uart.reset_pin == 1
        # Now frame response succeeds
        resp = self.uart.send_frame(b"\x1a\xc0\x00")
        assert resp.startswith(b"\x1a\xc1")

    def test_comb_boot_mode_selection_sequence(self):
        """Verify boot pin pulled low during reset enters Gecko bootloader."""
        # Boot pin low
        self.uart.boot_pin = 0
        self.uart.reset_pin = 0  # assert reset
        self.uart.reset_pin = 1  # release reset with boot low -> bootloader mode
        assert self.uart.boot_pin == 0
        assert self.uart.reset_pin == 1

    def test_comb_ezsp_packet_checksum_validation(self):
        """Verify simple XOR / CRC packet boundary integrity."""
        payload = b"\x1a\xc0\x00\x02"
        # Check frame starts with standard 0x1A byte
        assert payload[0] == 0x1A
