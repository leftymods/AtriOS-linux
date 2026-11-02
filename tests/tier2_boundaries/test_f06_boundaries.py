"""Tier 2: Boundary & Corner Cases - Feature F06."""

import pytest
from common.mock_subsystems import MockRotaryEncoder


@pytest.mark.tier2
@pytest.mark.feature("F06")
@pytest.mark.milestone("M2")
class TestF06Boundaries:
    """Boundary conditions for Gray code illegal transitions, bounce, and volume clamping."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.encoder = MockRotaryEncoder()

    def test_f06_illegal_double_hop_rejected(self):
        """Verify jump across diagonal (00 -> 11) is rejected (returns 0)."""
        self.encoder.state = 0  # 00
        step = self.encoder.step(1, 1)  # 11
        assert step == 0, "Illegal diagonal state jump 00 -> 11 was not rejected"

    def test_f06_contact_bounce_filtering(self):
        """Verify contact bounce (00 -> 01 -> 00) produces net zero change."""
        initial_vol = self.encoder.volume
        self.encoder.step(0, 1)  # +1
        self.encoder.step(0, 0)  # -1
        assert self.encoder.volume == initial_vol, "Contact bounce caused volume drift"

    def test_f06_volume_clamped_at_zero(self):
        """Verify volume cannot be decremented below 0."""
        self.encoder.volume = 0
        for _ in range(5):
            for a, b in MockRotaryEncoder.CCW_STEPS[1:]:
                self.encoder.step(a, b)
        assert self.encoder.volume == 0, f"Volume went below 0: {self.encoder.volume}"

    def test_f06_volume_clamped_at_maximum(self):
        """Verify volume cannot be incremented above 100."""
        self.encoder.volume = 100
        for _ in range(5):
            for a, b in MockRotaryEncoder.CW_STEPS[1:]:
                self.encoder.step(a, b)
        assert self.encoder.volume == 100, f"Volume exceeded 100: {self.encoder.volume}"

    def test_f06_static_pin_levels_no_change(self):
        """Verify repeated identical pin levels produce 0 direction step."""
        self.encoder.step(1, 0)
        step = self.encoder.step(1, 0)
        assert step == 0
