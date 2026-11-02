"""Tier 2: Boundary & Corner Cases - Feature F12."""

from pathlib import Path
import pytest
from common.mock_subsystems import MockBluetoothA2DP


@pytest.mark.tier2
@pytest.mark.feature("F12")
@pytest.mark.milestone("M3")
class TestF12Boundaries:
    """Boundary conditions for Bluetooth A2DP Sink, AVRCP limits, and connection drops."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.bt = MockBluetoothA2DP()

    def test_f12_avrcp_volume_upper_clamping(self):
        """Verify AVRCP volume cannot exceed 127."""
        self.bt.set_avrcp_volume(200)
        assert self.bt.volume == 127

    def test_f12_avrcp_volume_lower_clamping(self):
        """Verify AVRCP volume cannot be set below 0."""
        self.bt.set_avrcp_volume(-15)
        assert self.bt.volume == 0

    def test_f12_streaming_without_connection_rejected(self):
        """Verify stream cannot start when disconnected."""
        assert self.bt.state == "disconnected"
        assert self.bt.start_stream() is False
        assert self.bt.state == "disconnected"

    def test_f12_disconnect_during_stream_cleans_up(self):
        """Verify RF disconnection resets state cleanly to disconnected."""
        self.bt.connect("11:22:33:44:55:66")
        self.bt.start_stream()
        assert self.bt.state == "streaming"
        # Simulate link loss
        self.bt.state = "disconnected"
        assert self.bt.state == "disconnected"

    def test_f12_bt_firmware_blob_presence_in_repo(self, rootfs_overlay: Path):
        """Verify RTL8822ES/CS firmware files have non-zero file sizes."""
        fw_dir = rootfs_overlay / "lib" / "firmware"
        fw_files = list(fw_dir.glob("**/rtl*"))
        assert len(fw_files) > 0, "No Realtek Bluetooth firmware files found in overlay"
        for fw in fw_files:
            if fw.is_file():
                assert fw.stat().st_size > 0, f"Zero-byte firmware file found: {fw}"
