"""Tier 3: Cross-Feature Combinations - U-Boot Gadget Driver & Defconfig Modernization."""

from pathlib import Path
import pytest
from common.aml_burn_inspector import AmlProtocolEngine


@pytest.mark.tier3
@pytest.mark.cross_feature
@pytest.mark.aml_burn
@pytest.mark.milestone("M2")
class TestCombUbootGadgetDefconfig:
    """Cross-verify integration between U-Boot defconfig and USB AML burn gadget driver."""

    @pytest.fixture(autouse=True)
    def setup(self, uboot_defconfig: Path):
        self.uboot_defconfig = uboot_defconfig
        self.engine = AmlProtocolEngine()

    def test_comb_defconfig_burning_tool_pid(self):
        """Cross-verify defconfig specifies USB Burning Tool PID 0xC003 rather than Fastboot 0xFADA."""
        assert self.uboot_defconfig.exists(), f"Defconfig missing at {self.uboot_defconfig}"
        content = self.uboot_defconfig.read_text(encoding="utf-8", errors="replace")

        # Fastboot PID 0xFADA prevents Windows USB Burning Tool from detecting the board
        assert "CONFIG_USB_GADGET_PRODUCT_NUM=0xc003" in content or "0xC003" in content, (
            "CONFIG_USB_GADGET_PRODUCT_NUM must be 0xc003 for USB Burning Tool compatibility"
        )
        assert "0xfada" not in content.lower(), (
            "CONFIG_USB_GADGET_PRODUCT_NUM must not use Fastboot PID 0xfada in mainline defconfig"
        )

    def test_comb_defconfig_dwc3_controller_binding(self):
        """Cross-verify defconfig binds DWC3 USB controller (SM1 SoC) rather than legacy DWC2."""
        content = self.uboot_defconfig.read_text(encoding="utf-8", errors="replace")

        assert "CONFIG_USB_GADGET_DWC2_OTG=y" not in content, (
            "Defconfig mistakenly enables DWC2 OTG on Amlogic SM1 (requires DWC3)"
        )
        assert "CONFIG_USB_DWC3=y" in content or "CONFIG_USB_DWC3_GADGET=y" in content, (
            "Defconfig missing CONFIG_USB_DWC3 gadget controller options"
        )

    def test_comb_defconfig_aml_burn_command_enabled(self):
        """Cross-verify that CONFIG_CMD_AML_BURN and CONFIG_USB_GADGET_AML_BURN are enabled."""
        content = self.uboot_defconfig.read_text(encoding="utf-8", errors="replace")

        assert "CONFIG_CMD_AML_BURN=y" in content, (
            "CONFIG_CMD_AML_BURN=y is missing from uboot_station_max_defconfig"
        )
        assert "CONFIG_USB_GADGET_AML_BURN=y" in content, (
            "CONFIG_USB_GADGET_AML_BURN=y is missing from uboot_station_max_defconfig"
        )

    def test_comb_bootcommand_aml_burn_trigger(self):
        """Cross-verify that CONFIG_BOOTCOMMAND initiates aml_burn when booted from USB."""
        content = self.uboot_defconfig.read_text(encoding="utf-8", errors="replace")

        assert "aml_burn" in content, "CONFIG_BOOTCOMMAND missing aml_burn invocation"
