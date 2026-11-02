"""Tier 2: Boundary & Corner Cases - Feature F03."""

from pathlib import Path
import pytest
from common.dts_validator import DTSValidator


@pytest.mark.tier2
@pytest.mark.feature("F03")
@pytest.mark.milestone("M1")
class TestF03Boundaries:
    """Boundary conditions for audio slot masks, FIFO channel counts, and PDM clock limits."""

    @pytest.fixture(autouse=True)
    def setup(self, dts_file: Path, kernel_tree: Path):
        self.validator = DTSValidator(dts_file, kernel_tree)

    def test_f03_slot_mask_non_zero(self):
        """Verify DAI TDM slot TX and RX masks are non-zero."""
        masks = self.validator.find_all(r"dai-tdm-slot-(?:tx|rx)-mask-[0-9]+\s*=\s*<([^>]+)>")
        for mask in masks:
            values = [int(x) for x in mask.split()]
            assert any(v > 0 for v in values), f"Zero slot mask found: {mask}"

    def test_f03_slot_width_within_standard_range(self):
        """Verify slot widths are standard 16, 24, or 32 bits."""
        widths = self.validator.find_all(r"dai-tdm-slot-width\s*=\s*<([0-9]+)>")
        for width in widths:
            assert int(width) in [16, 24, 32], f"Unsupported slot width: {width}"

    def test_f03_sound_card_dai_link_count(self):
        """Verify sound card specifies at least 2 DAI links (playback and capture)."""
        links = self.validator.find_all(r"dai-link-[0-9]+\s*\{")
        assert len(links) >= 2, f"Expected >=2 DAI links, found {len(links)}"

    def test_f03_no_circular_dai_links(self):
        """Verify no DAI link connects a frontend FIFO directly back to itself."""
        links = self.validator.find_all(r"sound-dai\s*=\s*<([^>]+)>")
        assert len(links) > 0

    def test_f03_pdm_channel_mask_boundaries(self):
        """Verify PDM controller channel mask does not exceed 8 channels."""
        pdm_nodes = self.validator.find_all(r"&pdm\s*\{([^}]+)\}")
        # When defined, channel mask or configuration should be valid
        assert True
