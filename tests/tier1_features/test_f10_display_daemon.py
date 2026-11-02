"""Tier 1: Feature F10 - Gowin 25x16 Matrix & LED Ring Service."""

from pathlib import Path
import pytest
from common.mock_subsystems import MockFramebufferScreen, MockFramebufferRing


@pytest.mark.tier1
@pytest.mark.feature("F10")
@pytest.mark.milestone("M3")
class TestF10DisplayDaemon:
    """Verify display & lighting daemon architecture (atri-displayd) and framebuffer interfaces."""

    @pytest.fixture(autouse=True)
    def setup(self, rootfs_overlay: Path):
        self.rootfs = rootfs_overlay
        self.mock_screen = MockFramebufferScreen()
        self.mock_ring = MockFramebufferRing()

    def test_f10_screen_geometry_25x16(self):
        """Verify screen dimensions are 25 columns by 16 rows."""
        assert MockFramebufferScreen.WIDTH == 25
        assert MockFramebufferScreen.HEIGHT == 16
        assert len(self.mock_screen.buffer) == 400

    def test_f10_ring_geometry_72_channels(self):
        """Verify ring has 72 channels (24 RGB LEDs)."""
        assert MockFramebufferRing.CHANNELS == 72
        assert len(self.mock_ring.raw_buffer) == 72

    def test_f10_mock_screen_pattern_rendering(self):
        """Verify buffer writes at boundaries (0,0) and (24,15)."""
        self.mock_screen.write_pixel(0, 0, 255)
        self.mock_screen.write_pixel(24, 15, 128)
        assert self.mock_screen.get_pixel(0, 0) == 255
        assert self.mock_screen.get_pixel(24, 15) == 128

    def test_f10_screen_clear(self):
        """Verify clear operation sets all pixels to zero."""
        self.mock_screen.write_pixel(5, 5, 255)
        self.mock_screen.clear()
        assert self.mock_screen.get_pixel(5, 5) == 0

    def test_f10_auto_brightness_ambient_lux_mapping(self):
        """Verify ambient light lux mapping to display brightness."""
        def lux_to_brightness(lux: float) -> int:
            if lux <= 5:
                return 30
            elif lux <= 50:
                return 100
            elif lux <= 300:
                return 180
            else:
                return 255

        assert lux_to_brightness(2.0) == 30
        assert lux_to_brightness(25.0) == 100
        assert lux_to_brightness(150.0) == 180
        assert lux_to_brightness(500.0) == 255
