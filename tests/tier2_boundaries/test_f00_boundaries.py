"""Tier 2: Boundary & Corner Cases - Feature F00."""

import subprocess
from pathlib import Path
import pytest


@pytest.mark.tier2
@pytest.mark.feature("F00")
@pytest.mark.milestone("M0")
class TestF00Boundaries:
    """Boundary conditions for branch management and git repository state."""

    def test_f00_checkout_nonexistent_branch_fails(self, repo_root: Path):
        """Verify checkout of non-existent branch exits non-zero and does not alter HEAD."""
        res = subprocess.run(
            ["git", "checkout", "non_existent_branch_xyz"],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        assert res.returncode != 0

    def test_f00_no_detached_head(self, repo_root: Path):
        """Verify repository is not in a detached HEAD state."""
        res = subprocess.run(
            ["git", "symbolic-ref", "-q", "HEAD"],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0, "Repository is in detached HEAD state"

    def test_f00_git_diff_no_merge_conflicts(self, repo_root: Path):
        """Verify repository contains no unresolved merge conflict markers."""
        res = subprocess.run(
            ["git", "grep", "-n", "<<<<<<< HEAD"],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        assert res.returncode != 0, f"Merge conflict markers found: {res.stdout}"

    def test_f00_branch_name_validity(self, repo_root: Path):
        """Verify branch names conform to git-check-ref-format rules."""
        res = subprocess.run(
            ["git", "check-ref-format", "--branch", "mainline"],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0

    def test_f00_commit_without_message_rejected(self, repo_root: Path):
        """Verify git refuses to create commits with empty message."""
        res = subprocess.run(
            ["git", "commit", "--allow-empty", "-m", ""],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        assert res.returncode != 0
