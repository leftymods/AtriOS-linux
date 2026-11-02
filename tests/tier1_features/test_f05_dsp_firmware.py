"""Tier 1: Feature F05 - SY6045S DSP Firmware Decoupling."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector
from common.firmware_checker import FirmwareChecker


@pytest.mark.tier1
@pytest.mark.feature("F05")
@pytest.mark.milestone("M2")
class TestF05DSPFirmware:
    """Verify SY6045S EQ/DRC binary/text settings and request_firmware loading."""

    @pytest.fixture(autouse=True)
    def setup(self, drivers_dir: Path, rootfs_overlay: Path):
        self.inspector = DriverInspector(drivers_dir)
        self.checker = FirmwareChecker(rootfs_overlay)

    def test_f05_firmware_files_present(self):
        """Verify presence of tweeters and woofer settings files in rootfs firmware directory."""
        res = self.checker.check_sy6045s_files()
        assert res["has_tweeters_settings"], "sy6045s-tweeters-settings.txt is missing"
        assert res["has_woofer_settings"], "sy6045s-woofer-settings.txt is missing"

    def test_f05_tweeters_settings_format(self, rootfs_overlay: Path):
        """Verify tweeters settings file parses into valid hex registers and payloads."""
        fw_path = rootfs_overlay / "lib" / "firmware" / "sy6045s-tweeters-settings.txt"
        if not fw_path.exists():
            fw_path = rootfs_overlay / "lib" / "firmware" / "atrios" / "sy6045s-tweeters-settings.txt"
        valid, count, regs = self.checker.parse_sy6045s_settings(fw_path)
        assert valid, "Failed to parse tweeters settings format"
        assert count > 50, f"Expected >50 register blocks, got {count}"
        assert any(0x2F <= r <= 0x3D for r in regs), "No EQ/DRC biquad registers found (0x2F-0x3D)"

    def test_f05_woofer_settings_format(self, rootfs_overlay: Path):
        """Verify woofer settings file parses into valid hex registers and payloads."""
        fw_path = rootfs_overlay / "lib" / "firmware" / "sy6045s-woofer-settings.txt"
        if not fw_path.exists():
            fw_path = rootfs_overlay / "lib" / "firmware" / "atrios" / "sy6045s-woofer-settings.txt"
        valid, count, regs = self.checker.parse_sy6045s_settings(fw_path)
        assert valid, "Failed to parse woofer settings format"
        assert count > 50, f"Expected >50 register blocks, got {count}"
        assert any(0x2F <= r <= 0x3D for r in regs), "No EQ/DRC biquad registers found (0x2F-0x3D)"

    def test_f05_driver_request_firmware_used(self):
        """Verify snd-soc-sy6045s requests firmware via kernel firmware subsystem."""
        res = self.inspector.inspect_snd_sy6045s()
        assert res["requests_fw"], "Driver does not call request_firmware"
        assert res["supports_atrios_prefix"], "Driver lacks 'atrios/' firmware path support"

    def test_f05_burst_i2c_block_writes(self):
        """Verify driver uses i2c_smbus_write_i2c_block_data for atomic burst register writes."""
        res = self.inspector.inspect_snd_sy6045s()
        assert res["uses_burst_i2c"], "Driver does not perform burst I2C block writes"
