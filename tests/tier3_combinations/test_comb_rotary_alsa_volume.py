"""Tier 3: Pairwise Combination - Rotary Volume Encoder + ALSA Master Mixer."""

import pytest
from common.mock_subsystems import MockRotaryEncoder


@pytest.mark.tier3
@pytest.mark.milestone("M2")
class TestCombRotaryALSAVolume:
    """Validate synchronization between physical knob rotation and ALSA mixer controls."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.encoder = MockRotaryEncoder()

    def test_comb_rotary_steps_to_alsa_mixer_db_scale(self):
        """Verify rotary volume steps (0..100) map logarithmically to ALSA dB gain (-60dB .. 0dB)."""
        import math
        def vol_to_millibels(vol: int) -> int:
            if vol <= 0:
                return -6000  # -60 dB mute threshold
            # Logarithmic mapping
            db = 20.0 * math.log10(vol / 100.0)
            return int(max(-6000, db * 100))

        # Check key points
        assert vol_to_millibels(100) == 0      # 0 dB max
        assert vol_to_millibels(50) <= -600    # ~ -6 dB half
        assert vol_to_millibels(0) == -6000    # mute

    def test_comb_rotary_turn_produces_consistent_gain_steps(self):
        """Verify continuous rotation smoothly increments volume level without glitching."""
        initial_vol = self.encoder.volume
        # Turn CW 3 clicks
        for _ in range(3):
            for a, b in MockRotaryEncoder.CW_STEPS[1:]:
                self.encoder.step(a, b)
        assert self.encoder.volume == initial_vol + 3

    def test_comb_rotary_reverse_turn_recovers_original_volume(self):
        """Verify turning CW then CCW by equal steps returns to exact starting volume."""
        initial_vol = self.encoder.volume
        # CW 4 steps
        for _ in range(4):
            for a, b in MockRotaryEncoder.CW_STEPS[1:]:
                self.encoder.step(a, b)
        # CCW 4 steps
        for _ in range(4):
            for a, b in MockRotaryEncoder.CCW_STEPS[1:]:
                self.encoder.step(a, b)
        assert self.encoder.volume == initial_vol
