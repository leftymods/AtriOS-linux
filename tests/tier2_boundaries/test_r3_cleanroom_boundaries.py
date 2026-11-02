"""Tier 2: Boundary & Corner Cases - Requirement R3: Cleanroom Forensics."""

import tempfile
from pathlib import Path
import pytest
from common.aml_burn_inspector import KitchenBlobAuditor


@pytest.mark.tier2
@pytest.mark.requirement("R3")
@pytest.mark.aml_burn
@pytest.mark.milestone("M1")
class TestR3CleanroomBoundaries:
    """Boundary and collision detection stress tests for cleanroom compliance (R3)."""

    @pytest.fixture(autouse=True)
    def setup(self, kitchen_dir: Path):
        self.auditor = KitchenBlobAuditor(kitchen_dir)

    def test_r3_boundary_auditor_handles_nonexistent_path(self):
        """Verify auditor returns empty collisions on non-existent path without crashing."""
        collisions = self.auditor.audit_target(Path("/nonexistent/path/for/testing"))
        assert collisions == []

    def test_r3_boundary_detects_synthetic_stock_collision(self):
        """Verify auditor reliably detects when a file with a known stock hash is introduced."""
        stock_hashes = self.auditor.get_stock_hashes()
        if not stock_hashes:
            pytest.skip("No stock hashes available in kitchen directory")

        # Pick one stock blob
        target_name = list(stock_hashes.keys())[0]
        expected_hash = stock_hashes[target_name]

        # Read actual bytes of that stock blob from kitchen
        kitchen_file = self.auditor.kitchen_level1 / target_name
        if not kitchen_file.exists():
            kitchen_file = self.auditor.kitchen_dir / target_name
        if not kitchen_file.exists():
            pytest.skip(f"Could not read {target_name} from kitchen")

        stock_content = kitchen_file.read_bytes()

        # Write to temporary file outside repo
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
            tmp.write(stock_content)
            tmp_path = Path(tmp.name)

        try:
            detected = self.auditor.audit_target(tmp_path)
            assert len(detected) == 1, f"Auditor failed to detect synthetic collision: {detected}"
            # Because multiple kitchen blobs may share the same hash (e.g. DDR_ENC.USB and UBOOT_ENC.USB),
            # verify that the detected blob name has the expected hash.
            matched_name = detected[0][1]
            assert stock_hashes[matched_name] == expected_hash
            assert detected[0][2] == expected_hash
        finally:
            tmp_path.unlink(missing_ok=True)

    def test_r3_boundary_empty_file_not_flagged_as_stock(self):
        """Verify empty file is not falsely flagged as proprietary stock blob."""
        with tempfile.NamedTemporaryFile(suffix=".empty", delete=False) as tmp:
            tmp_path = Path(tmp.name)

        try:
            detected = self.auditor.audit_target(tmp_path)
            assert len(detected) == 0, f"Empty file was falsely flagged: {detected}"
        finally:
            tmp_path.unlink(missing_ok=True)
