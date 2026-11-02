"""Tier 1: Feature F03 - Audio PDM & Loopback DTS Bindings."""

from pathlib import Path
import pytest
from common.dts_validator import DTSValidator


@pytest.mark.tier1
@pytest.mark.feature("F03")
@pytest.mark.milestone("M1")
class TestF03AudioDTS:
    """Verify audio DAI links, PDM 8-ch mic bindings, and AEC loopback masks."""

    @pytest.fixture(autouse=True)
    def setup_validator(self, dts_file: Path, kernel_tree: Path):
        self.validator = DTSValidator(dts_file, kernel_tree)

    def test_f03_pdm_controller_enabled(self):
        """Verify PDM digital microphone controller node is enabled."""
        res = self.validator.check_audio_pdm()
        assert res["pdm_node_referenced"], "Node &pdm is not declared in DTS"
        assert res["status_okay"], "&pdm status is not 'okay'"

    def test_f03_dmics_codec_present(self):
        """Verify generic dmic-codec is declared for 8-channel array."""
        res = self.validator.check_audio_pdm()
        assert res["dmics_codec_present"], "dmic-codec binding is missing"

    def test_f03_no_broken_tdm_handles(self):
        """Verify broken &tdmb and &tdmc labels are replaced with &tdmif_b and &tdmif_c."""
        res = self.validator.check_audio_loopback_and_fifos()
        assert res["no_broken_tdmb"], "Obsolete phandle &tdmb found"
        assert res["no_broken_tdmc"], "Obsolete phandle &tdmc found"

    def test_f03_tdm_interfaces_used(self):
        """Verify sound card links to tdmif_b and tdmif_c interfaces."""
        res = self.validator.check_audio_loopback_and_fifos()
        assert res["uses_tdmif_b"], "tdmif_b interface not referenced"
        assert res["uses_tdmif_c"], "tdmif_c interface not referenced"

    def test_f03_es7210_rx_slot_mask(self):
        """Verify ES7210 AEC feedback ADC has 2-channel RX slot mask configured."""
        res = self.validator.check_audio_loopback_and_fifos()
        assert res["has_es7210_rx_mask"], "dai-tdm-slot-rx-mask-0 = <1 1> is missing"
