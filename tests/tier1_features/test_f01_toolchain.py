"""Tier 1: Feature F01 - Toolchain & Build Dependencies."""

import shutil
import subprocess
import pytest


@pytest.mark.tier1
@pytest.mark.feature("F01")
@pytest.mark.milestone("M1")
class TestF01Toolchain:
    """Verify presence and executable capability of build tools."""

    def test_f01_aarch64_gcc_installed(self):
        """Verify cross-compiler aarch64-linux-gnu-gcc is present and functional."""
        gcc_path = shutil.which("aarch64-linux-gnu-gcc")
        assert gcc_path is not None, "aarch64-linux-gnu-gcc not found in PATH"
        res = subprocess.run([gcc_path, "--version"], capture_output=True, text=True)
        assert res.returncode == 0
        assert "aarch64" in res.stdout.lower()

    def test_f01_dtc_installed(self):
        """Verify device tree compiler dtc is present."""
        dtc_path = shutil.which("dtc")
        assert dtc_path is not None, "dtc not found in PATH"
        res = subprocess.run([dtc_path, "--version"], capture_output=True, text=True)
        assert res.returncode == 0
        assert "version" in res.stdout.lower()

    def test_f01_python3_pyelftools_installed(self):
        """Verify python3-pyelftools module is importable (needed by U-Boot binman)."""
        res = subprocess.run(
            ["python3", "-c", "import elftools; print(elftools.__name__)"],
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0, f"pyelftools import failed: {res.stderr.strip()}"
        assert "elftools" in res.stdout

    def test_f01_make_installed(self):
        """Verify GNU make is available."""
        make_path = shutil.which("make")
        assert make_path is not None, "make not found in PATH"
        res = subprocess.run([make_path, "--version"], capture_output=True, text=True)
        assert res.returncode == 0
        assert "gnu make" in res.stdout.lower()

    def test_f01_build_parsers_installed(self):
        """Verify flex, bison, and bc are installed for kernel/U-Boot kconfig."""
        for tool in ["bison", "flex", "bc"]:
            assert shutil.which(tool) is not None, f"Required build tool '{tool}' is missing"
