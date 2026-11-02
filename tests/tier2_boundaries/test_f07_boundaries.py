"""Tier 2: Boundary & Corner Cases - Feature F07."""

import pytest
from common.mock_subsystems import MockFramebufferRing


@pytest.mark.tier2
@pytest.mark.feature("F07")
@pytest.mark.milestone("M2")
class TestF07Boundaries:
    """Boundary conditions for LED ring brightness clipping, channel bounds, and saturation."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.ring = MockFramebufferRing()

    def test_f07_brightness_overflow_clamping(self):
        """Verify brightness values > 255 are clamped to 255."""
        self.ring.set_backlight(300)
        assert self.ring.brightness == 255

    def test_f07_brightness_underflow_clamping(self):
        """Verify negative brightness values are clamped to 0."""
        self.ring.set_backlight(-50)
        assert self.ring.brightness == 0

    def test_f07_channel_buffer_length_fixed(self):
        """Verify ring buffer strictly maintains 72 channels without expanding."""
        assert len(self.ring.raw_buffer) == 72

    def test_f07_pixel_full_scale_saturation(self):
        """Verify maximum pixel input (255) at maximum backlight produces exactly 255."""
        self.ring.set_backlight(255)
        assert self.ring.apply_brightness_scaling(255) == 255

    def test_f07_all_channels_sync_dimensions(self):
        """Verify hardware sync returns exactly 72 scaled channel values."""
        self.ring.set_backlight(100)
        output = self.ring.sync()
        assert len(output) == 72
        assert all(isinstance(x, int) for x in output)
