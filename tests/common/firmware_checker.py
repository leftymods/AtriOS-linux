"""Firmware validator for DSP, FPGA, Bluetooth, and Codec assets."""

from pathlib import Path
from typing import Dict, List, Optional, Tuple


class FirmwareChecker:
    """Checker for DSP EQ/DRC, FPGA bitstream, and wireless firmware."""

    def __init__(self, rootfs_overlay: Path):
        self.rootfs_overlay = Path(rootfs_overlay)
        self.fw_dir = self.rootfs_overlay / "lib" / "firmware"

    def check_sy6045s_files(self) -> Dict[str, bool]:
        """Verify presence of SY6045S settings files."""
        # Check standard and atrios/ subpath
        tweeters_root = self.fw_dir / "sy6045s-tweeters-settings.txt"
        tweeters_sub = self.fw_dir / "atrios" / "sy6045s-tweeters-settings.txt"
        woofer_root = self.fw_dir / "sy6045s-woofer-settings.txt"
        woofer_sub = self.fw_dir / "atrios" / "sy6045s-woofer-settings.txt"

        has_tweeters = tweeters_root.exists() or tweeters_sub.exists()
        has_woofer = woofer_root.exists() or woofer_sub.exists()
        has_atrios_subfolder = (self.fw_dir / "atrios").is_dir() or tweeters_sub.exists()

        return {
            "has_tweeters_settings": has_tweeters,
            "has_woofer_settings": has_woofer,
            "has_atrios_subfolder": has_atrios_subfolder,
        }

    def parse_sy6045s_settings(self, file_path: Path) -> Tuple[bool, int, List[int]]:
        """Parse SY6045S ASCII settings file into register address and payload blocks.
        
        Returns (valid, line_count, register_list).
        """
        if not file_path.exists():
            return (False, 0, [])

        lines = file_path.read_text(encoding="utf-8", errors="replace").splitlines()
        reg_list = []
        valid_lines = 0

        for line in lines:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            try:
                reg = int(parts[0], 16)
                payload = [int(p, 16) for p in parts[1:]]
                reg_list.append(reg)
                valid_lines += 1
            except ValueError:
                return (False, valid_lines, reg_list)

        return (valid_lines > 0, valid_lines, reg_list)

    def check_fpga_bitstream(self) -> Dict[str, bool]:
        """Verify Gowin FPGA 25x16 bitstream and manifest."""
        fpga_bin = self.fw_dir / "yandex_led_screen_fpga.bin"
        fpga_manifest = self.fw_dir / "yandex_led_screen_fpga.bin.manifest"
        panel_bin = self.fw_dir / "yandex_led_panel.bin"

        has_bin = fpga_bin.exists() or panel_bin.exists()
        bin_size_valid = fpga_bin.exists() and fpga_bin.stat().st_size > 10000
        has_manifest = fpga_manifest.exists()

        return {
            "has_fpga_bin": has_bin,
            "bin_size_valid": bin_size_valid,
            "has_manifest": has_manifest,
        }

    def check_bluetooth_firmware(self) -> Dict[str, bool]:
        """Verify RTL8822CS Bluetooth firmware files and rtl_bt directory structure."""
        rtlbt_dir = self.fw_dir / "rtlbt"
        rtl_bt_dir = self.fw_dir / "rtl_bt"

        has_dir = rtlbt_dir.exists() or rtl_bt_dir.exists()
        has_symlink_or_dir = (self.fw_dir / "rtl_bt").exists()

        return {
            "has_rtlbt_directory": has_dir,
            "has_rtl_bt_path": has_symlink_or_dir,
        }

    def check_tas5825m_firmware(self) -> Dict[str, bool]:
        """Verify TAS5825M binary tuning files."""
        dvt_mihi = self.fw_dir / "tas5825m-mihi-dvt.bin"
        pvt_mihi = self.fw_dir / "tas5825m-mihi-pvt.bin"
        return {
            "has_dvt_tuning": dvt_mihi.exists(),
            "has_pvt_tuning": pvt_mihi.exists(),
        }
