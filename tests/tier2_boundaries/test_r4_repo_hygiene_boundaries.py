"""Tier 2: Boundary & Corner Cases - Requirement R4: Repo Hygiene & Build Script Robustness."""

import tempfile
from pathlib import Path
import pytest
from common.aml_burn_inspector import RepoHygieneAuditor


@pytest.mark.tier2
@pytest.mark.requirement("R4")
@pytest.mark.aml_burn
@pytest.mark.milestone("M3")
class TestR4RepoHygieneBoundaries:
    """Boundary and robustness tests for repository cleanliness and build script errors (R4)."""

    @pytest.fixture(autouse=True)
    def setup(self):
        self.auditor = RepoHygieneAuditor()

    def test_r4_boundary_build_script_pipefail_directive(self, uboot_script: Path):
        """Verify build_mainline_uboot.sh enforces 'set -euo pipefail' to prevent error masking."""
        assert uboot_script.exists(), f"Build script missing at {uboot_script}"
        lines = uboot_script.read_text(encoding="utf-8", errors="replace").splitlines()
        has_pipefail = any("pipefail" in line and "set" in line for line in lines[:20])
        assert has_pipefail, "build_mainline_uboot.sh missing 'set -euo pipefail' error trapping"

    def test_r4_boundary_detects_synthetic_built_in_archive(self):
        """Verify auditor correctly detects any built-in.a file placed in drivers."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_root = Path(tmp_dir)
            fake_driver_dir = tmp_root / "drivers" / "atrios"
            fake_driver_dir.mkdir(parents=True)
            fake_archive = fake_driver_dir / "built-in.a"
            fake_archive.write_bytes(b"!<arch>\n")

            detected = self.auditor.check_untracked_built_in(tmp_root)
            assert len(detected) == 1
            assert detected[0] == fake_archive

    def test_r4_boundary_detects_synthetic_kitchen_reference_script(self):
        """Verify auditor flags any script containing references to AmlogicKitchen/level1."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_root = Path(tmp_dir)
            scripts_dir = tmp_root / "scripts"
            scripts_dir.mkdir(parents=True)
            bad_script = scripts_dir / "build_aml_burn_package.py"
            bad_script.write_text('orig_dump = "/home/lmods/AmlogicKitchen/level1"', encoding="utf-8")

            detected = self.auditor.check_stock_copying_scripts(tmp_root)
            assert len(detected) == 1
            assert detected[0] == bad_script
