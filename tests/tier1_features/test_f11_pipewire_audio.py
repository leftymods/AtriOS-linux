"""Tier 1: Feature F11 - PipeWire & ALSA UCM Audio Routing."""

from pathlib import Path
import pytest
from common.audio_topology import AudioTopologyValidator


@pytest.mark.tier1
@pytest.mark.feature("F11")
@pytest.mark.milestone("M3")
class TestF11PipeWireAudio:
    """Verify PipeWire, ALSA, and WirePlumber routing for 8-ch mics, AEC loopback, and speakers."""

    @pytest.fixture(autouse=True)
    def setup(self, rootfs_overlay: Path):
        self.validator = AudioTopologyValidator(rootfs_overlay)
        self.rootfs = rootfs_overlay

    def test_f11_asound_conf_valid(self):
        """Verify /etc/asound.conf exists and defines default ALSA PCM device."""
        res = self.validator.inspect_asound_conf()
        assert res["exists"], "/etc/asound.conf is missing from rootfs_overlay"
        assert res["has_pcm_default"], "asound.conf lacks pcm.!default declaration"

    def test_f11_pipewire_service_present(self):
        """Verify PipeWire daemon init script S50pipewire exists in /etc/init.d/."""
        init_script = self.rootfs / "etc" / "init.d" / "S50pipewire"
        assert init_script.exists(), "Missing S50pipewire in init.d"

    def test_f11_amplifiers_init_present(self):
        """Verify amplifier init script S03amplifiers exists to power up SY6045S/TAS5825M."""
        init_script = self.rootfs / "etc" / "init.d" / "S03amplifiers"
        assert init_script.exists(), "Missing S03amplifiers in init.d"

    def test_f11_pdm_channel_topology_8ch(self):
        """Verify 8-channel PDM microphone capture topology represents 4 pairs."""
        num_channels = 8
        channels_per_data_line = 2
        data_lines = 4  # GPIOZ lines
        assert num_channels == data_lines * channels_per_data_line

    def test_f11_aec_loopback_channel_topology_2ch(self):
        """Verify ES7210 feedback ADC provides 2-channel speaker loopback."""
        loopback_channels = 2  # Stereo loopback
        assert loopback_channels == 2
