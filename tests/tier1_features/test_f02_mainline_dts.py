"""Tier 1: Feature F02 - Mainline DTS Repair."""

from pathlib import Path
import pytest
from common.dts_validator import DTSValidator


@pytest.mark.tier1
@pytest.mark.feature("F02")
@pytest.mark.milestone("M1")
class TestF02MainlineDTS:
    """Verify DTS syntax, mainline phandles, and required connector nodes."""

    @pytest.fixture(autouse=True)
    def setup_validator(self, dts_file: Path, kernel_tree: Path):
        self.validator = DTSValidator(dts_file, kernel_tree)

    def test_f02_hdmi_connector_present(self):
        """Verify HDMI connector node and endpoint link exist."""
        res = self.validator.check_hdmi_connector()
        assert res["node_present"], "HDMI connector node is missing"
        assert res["compatible_hdmi"], "HDMI connector node lacks 'hdmi-connector' compatible"

    def test_f02_emmc_pwrseq_present(self):
        """Verify eMMC pwrseq node is declared and referenced by mmc controller."""
        res = self.validator.check_emmc_pwrseq()
        assert res["pwrseq_node"], "emmc_pwrseq node is missing"
        assert res["mmc_reference"], "mmc node does not reference &emmc_pwrseq"

    def test_f02_emmc_pinctrl_valid(self):
        """Verify mainline eMMC pinctrl phandles are used without broken emmc_pins."""
        res = self.validator.check_emmc_pinctrl()
        assert res["has_no_broken_pins"], "Broken phandle &emmc_pins found"
        assert res["has_correct_pins"], "Valid eMMC pins phandles missing"

    def test_f02_uart_ao_b_pinctrl_valid(self):
        """Verify uart_ao_b uses valid pinctrl phandle."""
        res = self.validator.check_uart_ao_b_pinctrl()
        assert res["has_no_broken_reference"], "Broken phandle &uart_ao_b_pins found"

    def test_f02_dtc_clean_compilation(self):
        """Verify DTS compiles to DTB without missing phandles or syntax errors."""
        res = self.validator.compile_dtb()
        assert res.returncode == 0, f"DTS compilation failed: {res.stderr}"
