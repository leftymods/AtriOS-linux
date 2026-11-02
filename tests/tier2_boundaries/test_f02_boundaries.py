"""Tier 2: Boundary & Corner Cases - Feature F02."""

from pathlib import Path
import pytest
from common.dts_validator import DTSValidator


@pytest.mark.tier2
@pytest.mark.feature("F02")
@pytest.mark.milestone("M1")
class TestF02Boundaries:
    """Boundary conditions for Device Tree syntax, unresolved phandles, and duplicate nodes."""

    @pytest.fixture(autouse=True)
    def setup(self, dts_file: Path, kernel_tree: Path):
        self.validator = DTSValidator(dts_file, kernel_tree)

    def test_f02_no_unresolved_phandle_error_markers(self):
        """Verify DTS compilation stderr has no ERROR (phandle_references)."""
        res = self.validator.compile_dtb()
        assert "ERROR (phandle_references)" not in res.stderr, f"Unresolved phandles found: {res.stderr}"

    def test_f02_no_duplicate_label_definitions(self):
        """Verify no duplicate node label definitions in DTS."""
        labels = self.validator.find_all(r"([a-zA-Z0-9_]+):\s*[a-zA-Z0-9_-]+(\@[0-9a-fA-F]+)?\s*\{")
        label_names = [l[0] for l in labels]
        duplicates = [x for x in label_names if label_names.count(x) > 1]
        assert len(set(duplicates)) == 0, f"Duplicate labels found in DTS: {set(duplicates)}"

    def test_f02_reg_property_hex_formatting(self):
        """Verify all reg properties use valid hex numbers or 0."""
        regs = self.validator.find_all(r"reg\s*=\s*<([^>]+)>")
        for reg in regs:
            tokens = reg.split()
            for token in tokens:
                assert token.startswith("0x") or token.isdigit(), f"Malformed reg token: {token}"

    def test_f02_memory_node_non_empty(self):
        """Verify memory node reg contains non-zero RAM size."""
        mem_regs = self.validator.find_all(r"memory@[0-9a-fA-F]+\s*\{[^}]*reg\s*=\s*<([^>]+)>")
        assert len(mem_regs) > 0, "No memory node reg found"

    def test_f02_status_property_allowed_values(self):
        """Verify status properties only use standard 'okay' or 'disabled'."""
        statuses = self.validator.find_all(r"status\s*=\s*\"([^\"]+)\"")
        for status in statuses:
            assert status in ["okay", "disabled", "reserved", "fail"], f"Illegal status value: '{status}'"
