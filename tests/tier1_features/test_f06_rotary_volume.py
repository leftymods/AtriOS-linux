"""Tier 1: Feature F06 - Rotary Volume Gray Code Sampling."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector
from common.mock_subsystems import MockRotaryEncoder


@pytest.mark.tier1
@pytest.mark.feature("F06")
@pytest.mark.milestone("M2")
class TestF06RotaryVolume:
    """Verify synchronous quadrature Gray code decoder and input event generation."""

    @pytest.fixture(autouse=True)
    def setup(self, drivers_dir: Path):
        self.inspector = DriverInspector(drivers_dir)
        self.mock_encoder = MockRotaryEncoder()

    def test_f06_driver_state_machine_present(self):
        """Verify driver source implements quadrature Gray code state tracking."""
        res = self.inspector.inspect_rotary_volume()
        assert res["has_quadrature_state_machine"], "Quadrature state machine not implemented in rotary-volume.c"

    def test_f06_synchronous_edge_handling(self):
        """Verify driver avoids lossy delayed workqueue for edge transitions."""
        res = self.inspector.inspect_rotary_volume()
        assert res["no_lossy_workqueue"], "Driver uses lossy deferred workqueue for pin sampling"

    def test_f06_volume_key_events(self):
        """Verify driver reports standard Linux KEY_VOLUMEUP and KEY_VOLUMEDOWN input events."""
        res = self.inspector.inspect_rotary_volume()
        assert res["handles_volume_keys"], "Driver does not emit KEY_VOLUMEUP / KEY_VOLUMEDOWN"

    def test_f06_simulated_clockwise_rotation(self):
        """Verify that a full CW cycle (00 -> 01 -> 11 -> 10 -> 00) registers increments."""
        initial_vol = self.mock_encoder.volume
        for a, b in MockRotaryEncoder.CW_STEPS[1:]:
            self.mock_encoder.step(a, b)
        assert self.mock_encoder.volume > initial_vol, "CW rotation did not increment volume"

    def test_f06_simulated_counterclockwise_rotation(self):
        """Verify that a full CCW cycle (00 -> 10 -> 11 -> 01 -> 00) registers decrements."""
        initial_vol = self.mock_encoder.volume
        for a, b in MockRotaryEncoder.CCW_STEPS[1:]:
            self.mock_encoder.step(a, b)
        assert self.mock_encoder.volume < initial_vol, "CCW rotation did not decrement volume"
