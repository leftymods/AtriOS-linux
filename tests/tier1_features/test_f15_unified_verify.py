"""Tier 1: Feature F15 - Unified Verification Suite."""

from pathlib import Path
import os
import pytest


@pytest.mark.tier1
@pytest.mark.feature("F15")
@pytest.mark.milestone("M4")
class TestF15UnifiedVerify:
    """Verify automated verification and master build orchestration scripts."""

    def test_f15_verify_script_present(self, repo_root: Path):
        """Verify presence of scripts/verify_all.sh."""
        script = repo_root / "scripts" / "verify_all.sh"
        assert script.exists(), "scripts/verify_all.sh is missing"

    def test_f15_verify_script_executable(self, repo_root: Path):
        """Verify scripts/verify_all.sh is executable."""
        script = repo_root / "scripts" / "verify_all.sh"
        if script.exists():
            assert os.access(script, os.X_OK), "scripts/verify_all.sh is not executable"
        else:
            pytest.fail("scripts/verify_all.sh does not exist")

    def test_f15_master_build_script_present(self, repo_root: Path):
        """Verify build_atrios.sh exists in project root."""
        build_script = repo_root / "build_atrios.sh"
        assert build_script.exists(), "build_atrios.sh missing from project root"

    def test_f15_master_build_supports_mainline(self, repo_root: Path):
        """Verify build_atrios.sh references mainline kernel / U-Boot targets."""
        build_script = repo_root / "build_atrios.sh"
        content = build_script.read_text(encoding="utf-8", errors="replace")
        assert "mainline" in content or "linux_7.0" in content, "build_atrios.sh lacks mainline target"

    def test_f15_env_script_exports_toolchain(self, repo_root: Path):
        """Verify env.sh defines ARCH=arm64 and CROSS_COMPILE."""
        env_sh = repo_root / "env.sh"
        assert env_sh.exists(), "env.sh is missing"
        content = env_sh.read_text(encoding="utf-8", errors="replace")
        assert "arm64" in content, "ARCH=arm64 not defined in env.sh"
        assert "aarch64-linux-gnu-" in content, "CROSS_COMPILE prefix not defined in env.sh"
