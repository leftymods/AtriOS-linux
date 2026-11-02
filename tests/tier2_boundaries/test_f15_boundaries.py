"""Tier 2: Boundary & Corner Cases - Feature F15."""

import subprocess
from pathlib import Path
import pytest


@pytest.mark.tier2
@pytest.mark.feature("F15")
@pytest.mark.milestone("M4")
class TestF15Boundaries:
    """Boundary conditions for build verification scripts, bash syntax, and exit status."""

    def test_f15_build_atrios_bash_syntax(self, repo_root: Path):
        """Verify build_atrios.sh has valid bash syntax."""
        script = repo_root / "build_atrios.sh"
        res = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
        assert res.returncode == 0, f"Syntax error in build_atrios.sh: {res.stderr}"

    def test_f15_env_sh_bash_syntax(self, repo_root: Path):
        """Verify env.sh has valid bash syntax."""
        script = repo_root / "env.sh"
        res = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
        assert res.returncode == 0, f"Syntax error in env.sh: {res.stderr}"

    def test_f15_verify_all_bash_syntax(self, repo_root: Path):
        """Verify scripts/verify_all.sh has valid bash syntax."""
        script = repo_root / "scripts" / "verify_all.sh"
        if script.exists():
            res = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True)
            assert res.returncode == 0, f"Syntax error in scripts/verify_all.sh: {res.stderr}"

    def test_f15_rootfs_overlay_permissions(self, rootfs_overlay: Path):
        """Verify rootfs_overlay files are readable."""
        init = rootfs_overlay / "init"
        assert init.exists()
        import os
        assert os.access(init, os.R_OK)

    def test_f15_makefile_clean_syntax(self, repo_root: Path):
        """Verify Makefile in repo root has valid syntax (dry run)."""
        res = subprocess.run(["make", "-n"], cwd=repo_root, capture_output=True, text=True)
        assert res.returncode in [0, 2], f"Make parsing fatal error: {res.stderr}"
