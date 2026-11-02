"""Tier 1: Feature F07 - LED Ring Backlight & Framebuffer Name."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector
from common.mock_subsystems import MockFramebufferRing


@pytest.mark.tier1
@pytest.mark.feature("F07")
@pytest.mark.milestone("M2")
class TestF07LEDRing:
    """Verify LED ring framebuffer name compatibility and backlight brightness scaling."""

    @pytest.fixture(autouse=True)
    def setup(self, drivers_dir: Path):
        self.inspector = DriverInspector(drivers_dir)
        self.mock_ring = MockFramebufferRing()

    def test_f07_driver_brightness_math(self):
        """Verify driver calculates brightness as (pixel * ring_priv.brightness) / 255."""
        res = self.inspector.inspect_quasar_led_ring()
        assert res["has_correct_scaling"], "Brightness math in quasar_led_ring.c is broken"

    def test_f07_driver_fb_name_yndx(self):
        """Verify driver sets framebuffer fix.id to 'YNDX LED ring' for userspace compatibility."""
        res = self.inspector.inspect_quasar_led_ring()
        assert res["has_yndx_ring_id"], "fix.id is not 'YNDX LED ring'"

    def test_f07_ring_scaling_full_brightness(self):
        """Verify 100% backlight preserves pixel values."""
        self.mock_ring.set_backlight(255)
        assert self.mock_ring.apply_brightness_scaling(200) == 200

    def test_f07_ring_scaling_half_brightness(self):
        """Verify 50% backlight attenuates pixel values linearly."""
        self.mock_ring.set_backlight(127)
        scaled = self.mock_ring.apply_brightness_scaling(255)
        assert 126 <= scaled <= 128

    def test_f07_ring_scaling_zero_brightness(self):
        """Verify 0% backlight turns off all LEDs completely."""
        self.mock_ring.set_backlight(0)
        assert self.mock_ring.apply_brightness_scaling(255) == 0
        assert self.mock_ring.apply_brightness_scaling(128) == 0
