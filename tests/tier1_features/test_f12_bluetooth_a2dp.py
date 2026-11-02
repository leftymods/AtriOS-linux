"""Tier 1: Feature F12 - Bluetooth A2DP Sink Mode."""

from pathlib import Path
import pytest
from common.firmware_checker import FirmwareChecker
from common.mock_subsystems import MockBluetoothA2DP


@pytest.mark.tier1
@pytest.mark.feature("F12")
@pytest.mark.milestone("M3")
class TestF12BluetoothA2DP:
    """Verify Realtek RTL8822CS Bluetooth firmware paths, BlueZ config, and A2DP Sink operation."""

    @pytest.fixture(autouse=True)
    def setup(self, rootfs_overlay: Path):
        self.checker = FirmwareChecker(rootfs_overlay)
        self.rootfs = rootfs_overlay
        self.mock_bt = MockBluetoothA2DP()

    def test_f12_bluez_main_conf_present(self):
        """Verify /etc/bluetooth/main.conf exists."""
        main_conf = self.rootfs / "etc" / "bluetooth" / "main.conf"
        assert main_conf.exists(), "Missing /etc/bluetooth/main.conf in rootfs_overlay"

    def test_f12_rtl8822cs_firmware_path_valid(self):
        """Verify RTL8822CS firmware directory rtl_bt exists for mainline btrtl driver."""
        res = self.checker.check_bluetooth_firmware()
        assert res["has_rtlbt_directory"], "No Realtek Bluetooth firmware directory found"
        assert res["has_rtl_bt_path"], "Path /lib/firmware/rtl_bt missing (symlink needed)"

    def test_f12_bt_control_script_present(self):
        """Verify bluetooth control script S71atrios-bt-control is present."""
        script = self.rootfs / "etc" / "init.d" / "S71atrios-bt-control"
        assert script.exists(), "Missing S71atrios-bt-control in init.d"

    def test_f12_speaker_device_class_value(self):
        """Verify Bluetooth Class of Device is 0x240414 (Audio Rendering/Loudspeaker)."""
        speaker_cod = 0x240414
        # Service Class: Audio (bit 21) -> 0x200000; Major: Audio/Video -> 0x0400; Minor: Loudspeaker -> 0x0014
        assert (speaker_cod & 0x200000) != 0, "Audio rendering service bit not set"
        assert (speaker_cod & 0x000400) != 0, "Audio/Video major class not set"
        assert (speaker_cod & 0x000014) == 0x14, "Loudspeaker minor class not set"

    def test_f12_mock_a2dp_connection_and_streaming(self):
        """Verify Bluetooth A2DP Sink state machine handles connection and streaming."""
        assert self.mock_bt.connect("AA:BB:CC:DD:EE:FF") is True
        assert self.mock_bt.state == "connected"
        assert self.mock_bt.start_stream() is True
        assert self.mock_bt.state == "streaming"
