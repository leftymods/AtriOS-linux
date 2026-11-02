"""Tier 2: Boundary & Corner Cases - Feature F05."""

from pathlib import Path
import pytest
from common.firmware_checker import FirmwareChecker


@pytest.mark.tier2
@pytest.mark.feature("F05")
@pytest.mark.milestone("M2")
class TestF05Boundaries:
    """Boundary conditions for SY6045S firmware parsing, corrupted data, and missing files."""

    @pytest.fixture(autouse=True)
    def setup(self, rootfs_overlay: Path):
        self.checker = FirmwareChecker(rootfs_overlay)

    def test_f05_missing_firmware_returns_false(self, tmp_path: Path):
        """Verify parser returns (False, 0, []) on non-existent file path."""
        non_existent = tmp_path / "missing_file.txt"
        valid, count, regs = self.checker.parse_sy6045s_settings(non_existent)
        assert not valid
        assert count == 0
        assert len(regs) == 0

    def test_f05_empty_firmware_file_handled(self, tmp_path: Path):
        """Verify parser returns (False, 0, []) on empty file."""
        empty_file = tmp_path / "empty_fw.txt"
        empty_file.write_text("", encoding="utf-8")
        valid, count, regs = self.checker.parse_sy6045s_settings(empty_file)
        assert not valid
        assert count == 0

    def test_f05_corrupted_hex_characters_rejected(self, tmp_path: Path):
        """Verify parser rejects non-hex invalid characters."""
        corrupt_file = tmp_path / "corrupt_fw.txt"
        corrupt_file.write_text("0x2F 0xZZ 0x01\n", encoding="utf-8")
        valid, count, regs = self.checker.parse_sy6045s_settings(corrupt_file)
        assert not valid

    def test_f05_comment_lines_and_whitespace_ignored(self, tmp_path: Path):
        """Verify lines starting with # and blank lines are ignored without error."""
        fw_file = tmp_path / "commented_fw.txt"
        fw_file.write_text("# Comment line\n\n  # Indented comment\n0x2F 0x01 0x02\n", encoding="utf-8")
        valid, count, regs = self.checker.parse_sy6045s_settings(fw_file)
        assert valid
        assert count == 1
        assert regs == [0x2F]

    def test_f05_register_values_within_byte_range(self, rootfs_overlay: Path):
        """Verify all register addresses in actual firmware files fit in 8 bits (0..255)."""
        fw_path = rootfs_overlay / "lib" / "firmware" / "sy6045s-tweeters-settings.txt"
        if not fw_path.exists():
            fw_path = rootfs_overlay / "lib" / "firmware" / "atrios" / "sy6045s-tweeters-settings.txt"
        valid, count, regs = self.checker.parse_sy6045s_settings(fw_path)
        if valid:
            for r in regs:
                assert 0 <= r <= 255, f"Register address 0x{r:X} exceeds 8-bit limit"
