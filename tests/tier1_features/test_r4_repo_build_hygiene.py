"""Tier 1: Feature Coverage - Requirement R4: Repository Cleanliness & Build Integration."""

import os
import subprocess
from pathlib import Path
import pytest
from common.aml_burn_inspector import RepoHygieneAuditor


@pytest.mark.tier1
@pytest.mark.requirement("R4")
@pytest.mark.aml_burn
@pytest.mark.milestone("M3")
class TestR4RepoBuildHygiene:
    """Validate repository cleanliness and build script integrity contracts (R4)."""

    @pytest.fixture(autouse=True)
    def setup(self, repo_root: Path, uboot_script: Path):
        self.auditor = RepoHygieneAuditor()
        self.repo_root = repo_root
        self.uboot_script = uboot_script

    def test_r4_no_built_in_archive_in_drivers(self, repo_root: Path):
        """Verify drivers/atrios/built-in.a is purged from the working tree."""
        built_in_files = self.auditor.check_untracked_built_in(repo_root)
        assert len(built_in_files) == 0, (
            f"Repository cleanliness violation: in-tree archive found at {built_in_files}"
        )

    def test_r4_no_vendor_bl33_build_artifacts(self, repo_root: Path):
        """Verify vendor U-Boot 2015.01 build artifacts (bl33/build/) are removed."""
        bl33_build = repo_root / "bootloader" / "uboot-repo" / "bl33" / "build"
        assert not bl33_build.exists(), (
            f"Obsolete vendor U-Boot build debris found at {bl33_build}"
        )

    def test_r4_no_stock_copying_scripts(self, repo_root: Path):
        """Verify scripts copying from AmlogicKitchen (e.g. build_aml_burn_package.py) are purged."""
        violating_scripts = self.auditor.check_stock_copying_scripts(repo_root)
        assert len(violating_scripts) == 0, (
            f"Found scripts copying proprietary blobs from AmlogicKitchen: {violating_scripts}"
        )

    def test_r4_build_script_no_zero_pad_workaround(self, uboot_script: Path):
        """Verify build_mainline_uboot.sh does not substitute ACS with zero-padding."""
        assert uboot_script.exists(), f"Build script missing at {uboot_script}"
        has_zero_pad = self.auditor.check_build_script_zero_padding(uboot_script)
        assert not has_zero_pad, (
            "build_mainline_uboot.sh still contains zero-padding workaround for ACS / BL301!"
        )

    def test_r4_build_script_syntax_and_permissions(self, uboot_script: Path):
        """Verify build_mainline_uboot.sh is executable and has valid bash syntax."""
        assert os.access(uboot_script, os.X_OK) or uboot_script.stat().st_mode & 0o111, (
            f"Script {uboot_script} is not executable"
        )
        res = subprocess.run(
            ["bash", "-n", str(uboot_script)],
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0, f"bash -n syntax check failed: {res.stderr}"

    def test_r4_build_script_defines_usb_outputs(self, uboot_script: Path):
        """Verify build_mainline_uboot.sh defines u-boot.bin.usb.bl2 and u-boot.bin.usb.tpl."""
        content = uboot_script.read_text(encoding="utf-8", errors="replace")
        assert "usb.bl2" in content, "build_mainline_uboot.sh missing u-boot.bin.usb.bl2 target"
        assert "usb.tpl" in content, "build_mainline_uboot.sh missing u-boot.bin.usb.tpl target"
