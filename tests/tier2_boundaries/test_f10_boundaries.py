"""Tier 2: Boundary & Corner Cases - Feature F10."""

import pytest
from common.mock_subsystems import MockFramebufferScreen


@pytest.mark.tier2
@pytest.mark.feature("F10")
@pytest.mark.milestone("M3")
class TestF10Boundaries:
    """Boundary conditions for 25x16 LED screen drawing, clipping, and lux extremes."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.screen = MockFramebufferScreen()

    def test_f10_pixel_out_of_bounds_x_ignored(self):
        """Verify writes at x >= 25 do not overflow buffer."""
        initial_buf = bytes(self.screen.buffer)
        self.screen.write_pixel(25, 5, 255)
        assert bytes(self.screen.buffer) == initial_buf

    def test_f10_pixel_out_of_bounds_y_ignored(self):
        """Verify writes at y >= 16 do not overflow buffer."""
        initial_buf = bytes(self.screen.buffer)
        self.screen.write_pixel(10, 16, 255)
        assert bytes(self.screen.buffer) == initial_buf

    def test_f10_pixel_negative_coordinates_ignored(self):
        """Verify negative x or y coordinate writes are safely discarded."""
        initial_buf = bytes(self.screen.buffer)
        self.screen.write_pixel(-1, 0, 255)
        self.screen.write_pixel(0, -1, 255)
        assert bytes(self.screen.buffer) == initial_buf

    def test_f10_pixel_intensity_clamping(self):
        """Verify pixel values > 255 are clamped to 255."""
        self.screen.write_pixel(12, 8, 300)
        assert self.screen.get_pixel(12, 8) == 255

    def test_f10_screen_dimensions_strict(self):
        """Verify exact 400 bytes geometry (25 * 16)."""
        assert len(self.screen.buffer) == 400
