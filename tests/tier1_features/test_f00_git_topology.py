"""Tier 1: Feature F00 - Branch Management & Main Restoration."""

import subprocess
from pathlib import Path
import pytest


@pytest.mark.tier1
@pytest.mark.feature("F00")
@pytest.mark.milestone("M0")
class TestF00GitTopology:
    """Validate git branch topology, active branch, and remote configuration."""

    def test_f00_mainline_branch_exists(self, repo_root: Path):
        """Verify that branch 'mainline' exists in the repository."""
        res = subprocess.run(
            ["git", "branch", "--list", "mainline"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert "mainline" in res.stdout, "Branch 'mainline' does not exist"

    def test_f00_current_branch_is_mainline(self, repo_root: Path):
        """Verify that HEAD is checked out to 'mainline'."""
        res = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        current_branch = res.stdout.strip()
        assert current_branch == "mainline", f"Expected current branch 'mainline', got '{current_branch}'"

    def test_f00_main_branch_exists(self, repo_root: Path):
        """Verify that branch 'main' exists in the repository."""
        res = subprocess.run(
            ["git", "branch", "--list", "main"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert "main" in res.stdout, "Branch 'main' does not exist"

    def test_f00_origin_remote_configured(self, repo_root: Path):
        """Verify that git remote origin points to leftymods/MainlineAtriOS.git."""
        res = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        url = res.stdout.strip()
        assert "leftymods/MainlineAtriOS" in url, f"Unexpected remote URL: {url}"

    def test_f00_git_author_identity(self, repo_root: Path):
        """Verify that git user name and email match leftymods identity."""
        res_name = subprocess.run(
            ["git", "config", "user.name"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        res_email = subprocess.run(
            ["git", "config", "user.email"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert res_name.stdout.strip() == "leftymods", f"Expected 'leftymods', got '{res_name.stdout.strip()}'"
        assert res_email.stdout.strip() == "ggalab33@gmail.com", f"Expected 'ggalab33@gmail.com', got '{res_email.stdout.strip()}'"

    def test_f00_upstream_tracking(self, repo_root: Path):
        """Verify that local main tracks origin/main and mainline tracks origin/mainline."""
        res = subprocess.run(
            ["git", "branch", "-vv"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        output = res.stdout
        assert "main" in output and "[origin/main]" in output, "Branch 'main' is not tracking 'origin/main'"
        assert "mainline" in output and "[origin/mainline]" in output, "Branch 'mainline' is not tracking 'origin/mainline'"

    def test_f00_branch_head_commits(self, repo_root: Path):
        """Verify that main points to 1ea52e58 and mainline points to 7e16cf77."""
        res_main = subprocess.run(
            ["git", "rev-parse", "--short", "main"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        res_mainline = subprocess.run(
            ["git", "rev-parse", "--short", "mainline"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert res_main.stdout.strip() == "1ea52e58", f"Expected main commit 1ea52e58, got {res_main.stdout.strip()}"
        assert res_mainline.stdout.strip() == "7e16cf77", f"Expected mainline commit 7e16cf77, got {res_mainline.stdout.strip()}"

    def test_f00_mainline_assets_present(
        self,
        dts_file: Path,
        kernel_defconfig: Path,
        uboot_defconfig: Path,
        uboot_script: Path,
        drivers_dir: Path,
    ):
        """Verify that critical mainline assets exist on the filesystem."""
        assert dts_file.is_file(), f"Missing mainline DTS: {dts_file}"
        assert kernel_defconfig.is_file(), f"Missing kernel defconfig: {kernel_defconfig}"
        assert uboot_defconfig.is_file(), f"Missing uboot defconfig: {uboot_defconfig}"
        assert uboot_script.is_file(), f"Missing uboot build script: {uboot_script}"
        assert drivers_dir.is_dir(), f"Missing drivers directory: {drivers_dir}"

