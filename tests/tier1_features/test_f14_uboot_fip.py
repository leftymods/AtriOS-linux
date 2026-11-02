"""Tier 1: Feature F14 - Mainline U-Boot Build & FIP Signing."""

from pathlib import Path
import pytest
from common.uboot_inspector import UBootInspector


@pytest.mark.tier1
@pytest.mark.feature("F14")
@pytest.mark.milestone("M4")
class TestF14UBootFIP:
    """Verify U-Boot v2025.01 defconfig, aml_burn recovery, and FIP signing pipeline."""

    @pytest.fixture(autouse=True)
    def setup(self, repo_root: Path):
        self.inspector = UBootInspector(repo_root)

    def test_f14_uboot_defconfig_prompt(self):
        """Verify prompt is customized to AtriOS-aml#."""
        res = self.inspector.inspect_defconfig()
        assert res["has_atrios_prompt"], "CONFIG_SYS_PROMPT is not set to 'AtriOS-aml# '"

    def test_f14_uboot_defconfig_leftymods_identity(self):
        """Verify IDENT_STRING and USB_GADGET_MANUFACTURER specify leftymods."""
        res = self.inspector.inspect_defconfig()
        assert res["has_leftymods_ident"], "CONFIG_IDENT_STRING missing 'leftymods'"
        assert res["has_manufacturer_leftymods"], "CONFIG_USB_GADGET_MANUFACTURER is not 'leftymods'"

    def test_f14_uboot_recovery_commands(self):
        """Verify FASTBOOT, DFU, and aml_burn commands are enabled."""
        res = self.inspector.inspect_defconfig()
        assert res["has_cmd_fastboot"], "CONFIG_CMD_FASTBOOT=y is missing"
        assert res["has_cmd_dfu"], "CONFIG_CMD_DFU=y is missing"
        assert res["has_aml_burn_bootcmd"], "aml_burn is missing from CONFIG_BOOTCOMMAND"

    def test_f14_build_script_targets_v2025_01(self):
        """Verify build_mainline_uboot.sh references U-Boot v2025.01."""
        res = self.inspector.inspect_build_script()
        assert res["targets_v2025_01"], "scripts/build_mainline_uboot.sh does not target v2025.01"

    def test_f14_fip_signing_tool_and_blobs(self):
        """Verify aml_encrypt_g12a tool and bootloader blobs exist."""
        res = self.inspector.inspect_fip_binaries()
        assert res["has_encrypt_tool"], "aml_encrypt_g12a signing binary missing"
        assert res["has_bl2_blob"], "bl2 blob missing"
        assert res["has_bl30_blob"], "bl30 blob missing"
        assert res["has_bl31_blob"], "bl31 blob missing"
