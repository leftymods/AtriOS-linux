"""Tier 1: Feature F17 - Git Commit & GitHub Push."""

import subprocess
from pathlib import Path
import pytest


@pytest.mark.tier1
@pytest.mark.feature("F17")
@pytest.mark.milestone("M4")
class TestF17GitIdentity:
    """Verify git author configuration, commit signatures, and remote targets."""

    def test_f17_git_user_name_leftymods(self, repo_root: Path):
        """Verify git user.name is leftymods."""
        res = subprocess.run(
            ["git", "config", "user.name"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert res.stdout.strip() == "leftymods"

    def test_f17_git_user_email_ggalab33(self, repo_root: Path):
        """Verify git user.email is ggalab33@gmail.com."""
        res = subprocess.run(
            ["git", "config", "user.email"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert res.stdout.strip() == "ggalab33@gmail.com"

    def test_f17_head_commit_author(self, repo_root: Path):
        """Verify author of recent commits is leftymods."""
        res = subprocess.run(
            ["git", "log", "-n", "3", "--format=%an <%ae>"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        authors = res.stdout.strip().splitlines()
        assert any("leftymods" in a for a in authors), f"No commits authored by leftymods found in HEAD: {authors}"

    def test_f17_remote_push_url_origin(self, repo_root: Path):
        """Verify origin push URL points to MainlineAtriOS repo."""
        res = subprocess.run(
            ["git", "remote", "get-url", "--push", "origin"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert "MainlineAtriOS.git" in res.stdout

    def test_f17_clean_status_or_tracked_changes(self, repo_root: Path):
        """Verify git repo is in a valid state and tracks origin/mainline."""
        res = subprocess.run(
            ["git", "status", "--porcelain", "-b"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        first_line = res.stdout.splitlines()[0] if res.stdout else ""
        assert "mainline" in first_line, f"Branch tracking not on mainline: {first_line}"
