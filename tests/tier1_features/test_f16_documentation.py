"""Tier 1: Feature F16 - Documentation Overhaul."""

from pathlib import Path
import pytest


@pytest.mark.tier1
@pytest.mark.feature("F16")
@pytest.mark.milestone("M4")
class TestF16Documentation:
    """Verify documentation alignment, driver paths, and architecture specifications."""

    def test_f16_readme_references_drivers_atrios(self, repo_root: Path):
        """Verify README.md references modernized drivers/atrios/ path."""
        readme = repo_root / "README.md"
        assert readme.exists(), "README.md missing"
        content = readme.read_text(encoding="utf-8", errors="replace")
        assert "drivers/atrios" in content, "README.md does not reference 'drivers/atrios/'"

    def test_f16_readme_no_obsolete_drivers_yandex(self, repo_root: Path):
        """Verify README.md contains no obsolete references to drivers/yandex/."""
        readme = repo_root / "README.md"
        content = readme.read_text(encoding="utf-8", errors="replace")
        assert "drivers/yandex" not in content, "README.md still references obsolete path 'drivers/yandex/'"

    def test_f16_bluetooth_spec_present(self, repo_root: Path):
        """Verify docs/BLUETOOTH_SETUP_SPEC.md is present."""
        spec = repo_root / "docs" / "BLUETOOTH_SETUP_SPEC.md"
        assert spec.exists(), "docs/BLUETOOTH_SETUP_SPEC.md missing"

    def test_f16_dts_comparison_present(self, repo_root: Path):
        """Verify docs/dts-port-comparison.md is present."""
        doc = repo_root / "docs" / "dts-port-comparison.md"
        assert doc.exists(), "docs/dts-port-comparison.md missing"

    def test_f16_drivers_readme_present(self, drivers_dir: Path):
        """Verify drivers/atrios/README.md exists."""
        readme = drivers_dir / "README.md"
        assert readme.exists(), "drivers/atrios/README.md missing"
