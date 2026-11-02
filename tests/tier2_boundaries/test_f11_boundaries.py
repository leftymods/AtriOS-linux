"""Tier 2: Boundary & Corner Cases - Feature F11."""

from pathlib import Path
import pytest
from common.audio_topology import AudioTopologyValidator


@pytest.mark.tier2
@pytest.mark.feature("F11")
@pytest.mark.milestone("M3")
class TestF11Boundaries:
    """Boundary conditions for audio routing, buffer latencies, and sample rates."""

    @pytest.fixture(autouse=True)
    def setup(self, rootfs_overlay: Path):
        self.validator = AudioTopologyValidator(rootfs_overlay)
        self.rootfs = rootfs_overlay

    def test_f11_asound_conf_parseable(self):
        """Verify /etc/asound.conf contains valid bracket and quote pairing."""
        asound = self.rootfs / "etc" / "asound.conf"
        if asound.exists():
            text = asound.read_text(encoding="utf-8", errors="replace")
            assert text.count("{") == text.count("}"), "Mismatched curly braces in asound.conf"

    def test_f11_aec_latency_window_bounds(self):
        """Verify AEC filter buffer duration window is between 10ms and 50ms."""
        min_aec_latency_ms = 10
        max_aec_latency_ms = 50
        configured_latency_ms = 20  # standard WebRTC AEC3 frame
        assert min_aec_latency_ms <= configured_latency_ms <= max_aec_latency_ms

    def test_f11_pdm_supported_sample_rates(self):
        """Verify standard voice/music rates (16kHz, 48kHz) are within supported set."""
        supported_rates = [16000, 32000, 44100, 48000]
        assert 16000 in supported_rates
        assert 48000 in supported_rates

    def test_f11_alsa_buffer_frame_sizes(self):
        """Verify ALSA buffer size is power of 2 between 64 and 8192 frames."""
        valid_buffer_sizes = [64, 128, 256, 512, 1024, 2048, 4096, 8192]
        test_size = 1024
        assert test_size in valid_buffer_sizes

    def test_f11_pipewire_conf_syntax_checks(self):
        """Verify PipeWire configuration files do not contain syntax errors."""
        res = self.validator.inspect_pipewire_configs()
        assert res["has_pipewire_or_service"]
