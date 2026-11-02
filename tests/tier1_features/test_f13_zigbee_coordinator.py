"""Tier 1: Feature F13 - Zigbee Coordinator Integration."""

from pathlib import Path
import pytest
from common.dts_validator import DTSValidator
from common.driver_inspector import DriverInspector
from common.mock_subsystems import MockZigbeeUART


@pytest.mark.tier1
@pytest.mark.feature("F13")
@pytest.mark.milestone("M3")
class TestF13ZigbeeCoordinator:
    """Verify Silicon Labs EFR32 Zigbee coordinator UART, reset GPIOs, and EZSP protocol."""

    @pytest.fixture(autouse=True)
    def setup(self, dts_file: Path, kernel_tree: Path, drivers_dir: Path):
        self.dts_val = DTSValidator(dts_file, kernel_tree)
        self.inspector = DriverInspector(drivers_dir)
        self.mock_uart = MockZigbeeUART()

    def test_f13_uart_ao_b_enabled(self):
        """Verify UART_AO_B controller (/dev/ttyAML1) is enabled in DTS."""
        assert self.dts_val.has_node(r"&uart_AO_B\s*\{"), "uart_AO_B node missing from DTS"
        assert self.dts_val.has_property(r"&uart_AO_B\s*\{[^}]*status\s*=\s*\"okay\""), "uart_AO_B not status 'okay'"

    def test_f13_zigbee_control_driver_present(self):
        """Verify zigbee-control driver provides GPIO control attributes."""
        res = self.inspector.inspect_zigbee_control()
        assert res["has_sysfs_attrs"], "Missing GPIO sysfs attributes in zigbee-control.c"

    def test_f13_ezsp_protocol_baudrate(self):
        """Verify standard EZSP NCP protocol operates at 115200 baud."""
        assert self.mock_uart.baudrate == 115200

    def test_f13_mock_zigbee_reset_toggle(self):
        """Verify reset sequencing brings Zigbee coordinator to active state."""
        assert not self.mock_uart.ezsp_connected
        self.mock_uart.toggle_reset(duration_ms=100)
        assert self.mock_uart.ezsp_connected is True

    def test_f13_mock_ezsp_frame_exchange(self):
        """Verify response to EZSP frame 0x1A 0xC0 (version command)."""
        self.mock_uart.toggle_reset()
        response = self.mock_uart.send_frame(b"\x1a\xc0\x00\x01")
        assert response.startswith(b"\x1a\xc1"), f"Unexpected EZSP response: {response.hex()}"
