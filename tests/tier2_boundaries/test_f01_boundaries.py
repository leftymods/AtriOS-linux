"""Tier 2: Boundary & Corner Cases - Feature F01."""

import subprocess
from pathlib import Path
import pytest


@pytest.mark.tier2
@pytest.mark.feature("F01")
@pytest.mark.milestone("M1")
class TestF01Boundaries:
    """Boundary conditions for toolchain, compiler error paths, and missing packages."""

    def test_f01_gcc_invalid_architecture_flag(self):
        """Verify cross-compiler properly rejects invalid architecture target."""
        res = subprocess.run(
            ["aarch64-linux-gnu-gcc", "-march=invalid_arch_xyz", "-c", "/dev/null"],
            capture_output=True,
            text=True,
        )
        assert res.returncode != 0
        assert "error" in res.stderr.lower()

    def test_f01_dtc_empty_file_handling(self, tmp_path: Path):
        """Verify DTC gracefully handles completely empty DTS file."""
        empty_dts = tmp_path / "empty.dts"
        empty_dts.write_text("", encoding="utf-8")
        res = subprocess.run(
            ["dtc", "-I", "dts", "-O", "dtb", "-o", "/dev/null", str(empty_dts)],
            capture_output=True,
            text=True,
        )
        assert res.returncode != 0, "DTC should fail on empty DTS without root node"

    def test_f01_make_nonexistent_target(self, repo_root: Path):
        """Verify GNU make exits non-zero on non-existent target."""
        res = subprocess.run(
            ["make", "non_existent_target_12345"],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        assert res.returncode != 0

    def test_f01_python_import_nonexistent_module(self):
        """Verify Python module error reporting for non-existent module."""
        res = subprocess.run(
            ["python3", "-c", "import nonexistent_module_xyz"],
            capture_output=True,
            text=True,
        )
        assert res.returncode != 0
        assert "ModuleNotFoundError" in res.stderr

    def test_f01_pyelftools_version_or_member_integrity(self):
        """Verify pyelftools contains expected ELF parsing classes when installed."""
        res = subprocess.run(
            ["python3", "-c", "import elftools.elf.elffile; print(elftools.elf.elffile.ELFFile)"],
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0, f"pyelftools class check failed: {res.stderr}"
