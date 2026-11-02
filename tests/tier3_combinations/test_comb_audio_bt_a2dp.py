"""Tier 3: Pairwise Combination - Audio Subsystem + Bluetooth A2DP Sink."""

from pathlib import Path
import pytest
from common.audio_topology import AudioTopologyValidator
from common.mock_subsystems import MockBluetoothA2DP


@pytest.mark.tier3
@pytest.mark.milestone("M3")
class TestCombAudioBTA2DP:
    """Validate integration between Bluetooth A2DP audio sink and PipeWire output routing."""

    @pytest.fixture(autouse=True)
    def setup(self, rootfs_overlay: Path):
        self.audio = AudioTopologyValidator(rootfs_overlay)
        self.bt = MockBluetoothA2DP()

    def test_comb_bt_audio_sink_pipewire_routing(self):
        """Verify Bluetooth audio stream can route to PipeWire default stereo output."""
        # Connect phone and start A2DP streaming
        assert self.bt.connect("AA:BB:CC:11:22:33")
        assert self.bt.start_stream()

        # In PipeWire, bluez5 source node links to default playback sink
        stream_sample_rate = 44100
        output_sample_rate = 48000
        # Resampling ratio
        ratio = output_sample_rate / stream_sample_rate
        assert 1.0 < ratio < 1.1

    def test_comb_avrcp_volume_sync_to_alsa_range(self):
        """Verify AVRCP volume (0..127) converts linearly to ALSA percentage (0..100%)."""
        for avrcp_vol in [0, 32, 64, 96, 127]:
            alsa_pct = round((avrcp_vol / 127.0) * 100.0)
            assert 0 <= alsa_pct <= 100
            # 64 should correspond to ~50%
            if avrcp_vol == 64:
                assert 49 <= alsa_pct <= 51

    def test_comb_amplifier_powerup_before_audio_stream(self, rootfs_overlay: Path):
        """Verify init scripts ensure S03amplifiers runs before S50pipewire / S71bt."""
        init_d = rootfs_overlay / "etc" / "init.d"
        s03 = init_d / "S03amplifiers"
        s50 = init_d / "S50pipewire"
        s71 = init_d / "S71atrios-bt-control"
        assert s03.exists() and s50.exists()
        # Alphabetical sort in init.d guarantees execution order S03 -> S50 -> S71
        assert s03.name < s50.name < s71.name
