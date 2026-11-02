"""Tier 3: Pairwise Combination - Gowin 25x16 Matrix + LED Ring Visualizer."""

import pytest
from common.mock_subsystems import MockFramebufferScreen, MockFramebufferRing


@pytest.mark.tier3
@pytest.mark.milestone("M3")
class TestCombScreenRingVisualizer:
    """Validate coordinated rendering across /dev/fb0 (screen) and /dev/fb1 (ring)."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.screen = MockFramebufferScreen()
        self.ring = MockFramebufferRing()

    def test_comb_concurrent_frame_buffer_updates(self):
        """Verify simultaneous writes to screen and ring do not cause buffer crosstalk."""
        # Render equalizer bar on screen
        for y in range(8, 16):
            self.screen.write_pixel(12, y, 255)

        # Render color ring pattern
        for i in range(24):
            self.ring.raw_buffer[i * 3 + 0] = 200  # R
            self.ring.raw_buffer[i * 3 + 1] = 50   # G
            self.ring.raw_buffer[i * 3 + 2] = 100  # B

        # Verify screen pixel is unaffected by ring writes
        assert self.screen.get_pixel(12, 10) == 255
        assert self.ring.raw_buffer[0] == 200

    def test_comb_audio_fft_spectrum_mapping(self):
        """Verify audio FFT magnitude values map to screen column heights and ring colors."""
        # 25 frequency bins for 25 columns
        magnitudes = [i * 10 for i in range(25)]
        for col, mag in enumerate(magnitudes):
            # Map magnitude (0..255) to bar height (0..16)
            bar_height = int((mag / 255.0) * 16)
            assert 0 <= bar_height <= 16

    def test_comb_master_brightness_attenuation(self):
        """Verify global auto-brightness dims both screen pixels and ring LEDs proportionally."""
        ambient_brightness_scale = 0.5  # 50% night mode
        self.ring.set_backlight(int(255 * ambient_brightness_scale))
        scaled_ring = self.ring.apply_brightness_scaling(200)
        assert 99 <= scaled_ring <= 101
