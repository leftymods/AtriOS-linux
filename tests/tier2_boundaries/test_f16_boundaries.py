"""Tier 2: Boundary & Corner Cases - Feature F16."""

from pathlib import Path
import pytest


@pytest.mark.tier2
@pytest.mark.feature("F16")
@pytest.mark.milestone("M4")
class TestF16Boundaries:
    """Boundary conditions for documentation UTF-8 encoding, non-empty files, and path integrity."""

    def test_f16_readme_non_empty_size(self, repo_root: Path):
        """Verify README.md is non-empty (>500 bytes)."""
        readme = repo_root / "README.md"
        assert readme.exists()
        assert readme.stat().st_size > 500

    def test_f16_docs_utf8_clean_decode(self, repo_root: Path):
        """Verify all markdown documents decode cleanly as strict UTF-8."""
        docs = list(repo_root.glob("**/*.md"))
        for doc in docs:
            # Must not throw UnicodeDecodeError
            _ = doc.read_bytes().decode("utf-8")

    def test_f16_drivers_readme_non_empty(self, drivers_dir: Path):
        """Verify drivers/atrios/README.md has content."""
        readme = drivers_dir / "README.md"
        if readme.exists():
            assert readme.stat().st_size > 100

    def test_f16_dts_comparison_doc_non_empty(self, repo_root: Path):
        """Verify docs/dts-port-comparison.md has content."""
        doc = repo_root / "docs" / "dts-port-comparison.md"
        assert doc.exists()
        assert doc.stat().st_size > 100

    def test_f16_no_windows_cr_line_endings_in_scripts(self, repo_root: Path):
        """Verify shell scripts do not contain CRLF line endings (causes bad interpreter in Linux)."""
        scripts = list((repo_root / "scripts").glob("*.sh")) + [repo_root / "build_atrios.sh", repo_root / "env.sh"]
        for s in scripts:
            if s.exists():
                content = s.read_bytes()
                assert b"\r\n" not in content, f"Windows CRLF line endings found in shell script: {s}"
