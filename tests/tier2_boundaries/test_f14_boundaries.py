"""Tier 2: Boundary & Corner Cases - Feature F14."""

from pathlib import Path
import os
import pytest
from common.uboot_inspector import UBootInspector


@pytest.mark.tier2
@pytest.mark.feature("F14")
@pytest.mark.milestone("M4")
class TestF14Boundaries:
    """Boundary conditions for U-Boot env alignment, FIP prebuilt executable bits, and loadaddrs."""

    @pytest.fixture(autouse=True)
    def setup(self, repo_root: Path):
        self.inspector = UBootInspector(repo_root)
        self.fip_dir = repo_root / "bootloader" / "uboot-repo"

    def test_f14_aml_encrypt_tool_executable(self):
        """Verify aml_encrypt_g12a tool has executable permissions."""
        tool = self.fip_dir / "fip" / "g12a" / "aml_encrypt_g12a"
        if tool.exists():
            assert os.access(tool, os.X_OK), "aml_encrypt_g12a tool is not marked executable"

    def test_f14_env_offset_hex_format(self, repo_root: Path):
        """Verify CONFIG_ENV_OFFSET is valid hex value aligned to at least 4KB."""
        defconfig = repo_root / "board" / "yandex" / "station2" / "mainline" / "uboot_station_max_defconfig"
        content = defconfig.read_text(encoding="utf-8", errors="replace")
        for line in content.splitlines():
            if line.startswith("CONFIG_ENV_OFFSET="):
                val = line.split("=")[1].strip()
                offset = int(val, 16)
                assert offset % 4096 == 0, f"CONFIG_ENV_OFFSET {val} is not 4KB aligned"

    def test_f14_bl2_binary_size_bounds(self):
        """Verify BL2 prebuilt size is within Amlogic SRAM limit (< 64KB)."""
        bl2_files = list(self.fip_dir.glob("**/bl2.bin"))
        if bl2_files:
            bl2 = bl2_files[0]
            assert bl2.stat().st_size < 65536, f"BL2 exceeds 64KB SRAM limit: {bl2.stat().st_size}"

    def test_f14_bl30_binary_size_bounds(self):
        """Verify BL30 (SCP system control processor) size is within limit (< 64KB)."""
        bl30_files = list(self.fip_dir.glob("**/bl30.bin"))
        if bl30_files:
            bl30 = bl30_files[0]
            assert bl30.stat().st_size < 65536, f"BL30 exceeds 64KB limit: {bl30.stat().st_size}"

    def test_f14_build_script_syntax_valid(self, repo_root: Path):
        """Verify scripts/build_mainline_uboot.sh has valid bash syntax."""
        script = repo_root / "scripts" / "build_mainline_uboot.sh"
        if script.exists():
            import subprocess
            res = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
            assert res.returncode == 0, f"Syntax errors in build_mainline_uboot.sh: {res.stderr}"
