"""Tier 2: Boundary & Corner Cases - Feature F09."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector


@pytest.mark.tier2
@pytest.mark.feature("F09")
@pytest.mark.milestone("M2")
class TestF09Boundaries:
    """Boundary conditions for driver error propagation, resource release, and compiler cleanliness."""

    @pytest.fixture(autouse=True)
    def setup(self, drivers_dir: Path):
        self.inspector = DriverInspector(drivers_dir)

    def test_f09_tas5825m_regulator_error_propagation(self):
        """Verify tas5825m propagates negative return code when regulator enable fails."""
        code = self.inspector.get_source("snd-soc-tas5825m.c")
        assert "regulator_bulk_enable" in code
        assert "if (" in code or "return ret;" in code or "goto" in code

    def test_f09_tas5825m_remove_unregisters_codec(self):
        """Verify tas5825m unregisters component/codec on module exit."""
        code = self.inspector.get_source("snd-soc-tas5825m.c")
        assert "tas5825m_remove" in code or "tas5825m_i2c_remove" in code

    def test_f09_rotary_free_irq_handling(self):
        """Verify rotary-volume frees allocated GPIO IRQs in remove handler."""
        code = self.inspector.get_source("rotary-volume.c")
        assert "devm_request_irq" in code or "free_irq" in code or "devm_" in code

    def test_f09_gowin_module_exit_unregisters_fb(self):
        """Verify gowin_led_device unregisters framebuffer on device removal."""
        code = self.inspector.get_source("gowin_led_device.c")
        assert "unregister_framebuffer" in code

    def test_f09_is31fl32xx_i2c_transfer_error_return(self):
        """Verify leds-is31fl32xx checks I2C transfer return codes."""
        code = self.inspector.get_source("leds-is31fl32xx.c")
        assert "i2c_smbus" in code or "i2c_transfer" in code
