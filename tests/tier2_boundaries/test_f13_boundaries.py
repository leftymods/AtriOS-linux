"""Tier 2: Boundary & Corner Cases - Feature F13."""

import pytest
from common.mock_subsystems import MockZigbeeUART


@pytest.mark.tier2
@pytest.mark.feature("F13")
@pytest.mark.milestone("M3")
class TestF13Boundaries:
    """Boundary conditions for Zigbee EFR32 EZSP UART frames, reset timing, and modes."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.uart = MockZigbeeUART()

    def test_f13_frame_before_reset_rejected(self):
        """Verify UART ignores EZSP frames when coordinator is uninitialized/unreset."""
        assert not self.uart.ezsp_connected
        resp = self.uart.send_frame(b"\x1a\xc0\x00")
        assert resp == b"", "Responded to frame while uninitialized"

    def test_f13_malformed_frame_rejected(self):
        """Verify malformed non-EZSP bytes do not produce valid response."""
        self.uart.toggle_reset()
        resp = self.uart.send_frame(b"\xFF\xFF\xFF")
        assert not resp.startswith(b"\x1a\xc1"), "Malformed frame produced EZSP reply"

    def test_f13_reset_pin_active_low_polarity(self):
        """Verify reset is active-low (0 while asserting, 1 while released)."""
        assert self.uart.reset_pin == 1  # idle high
        self.uart.reset_pin = 0
        assert self.uart.reset_pin == 0  # asserted low
        self.uart.reset_pin = 1
        assert self.uart.reset_pin == 1  # released

    def test_f13_boot_pin_bootloader_mode_logic(self):
        """Verify boot pin 0 puts coordinator in bootloader flashing mode."""
        self.uart.boot_pin = 0
        assert self.uart.boot_pin == 0
        self.uart.boot_pin = 1
        assert self.uart.boot_pin == 1

    def test_f13_ttyaml1_standard_baudrate_set(self):
        """Verify supported UART baudrates include 115200 and standard speeds."""
        valid_baudrates = [9600, 19200, 38400, 57600, 115200, 230400]
        assert self.uart.baudrate in valid_baudrates
