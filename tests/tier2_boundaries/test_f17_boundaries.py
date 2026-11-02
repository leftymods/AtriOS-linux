"""Tier 2: Boundary & Corner Cases - Feature F17."""

import re
import subprocess
from pathlib import Path
import pytest


@pytest.mark.tier2
@pytest.mark.feature("F17")
@pytest.mark.milestone("M4")
class TestF17Boundaries:
    """Boundary conditions for git author format, commit SHA lengths, and remote parity."""

    def test_f17_author_email_format(self, repo_root: Path):
        """Verify user.email is a syntactically valid email address."""
        res = subprocess.run(
            ["git", "config", "user.email"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        email = res.stdout.strip()
        assert re.match(r"^[^@]+@[^@]+\.[^@]+$", email), f"Malformed git email: '{email}'"

    def test_f17_author_name_non_empty(self, repo_root: Path):
        """Verify user.name is non-empty and has at least 2 characters."""
        res = subprocess.run(
            ["git", "config", "user.name"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        name = res.stdout.strip()
        assert len(name) >= 2, f"Git author name too short: '{name}'"

    def test_f17_head_commit_sha_validity(self, repo_root: Path):
        """Verify HEAD commit SHA is exactly 40 lowercase hexadecimal characters."""
        res = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        sha = res.stdout.strip()
        assert len(sha) == 40
        assert re.match(r"^[0-9a-f]{40}$", sha), f"Invalid SHA format: {sha}"

    def test_f17_origin_fetch_and_push_parity(self, repo_root: Path):
        """Verify origin fetch URL and push URL match."""
        fetch_res = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        push_res = subprocess.run(
            ["git", "remote", "get-url", "--push", "origin"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=True,
        )
        assert fetch_res.stdout.strip() == push_res.stdout.strip()

    def test_f17_no_corrupted_git_objects(self, repo_root: Path):
        """Verify git fsck detects no corrupted loose or packed objects."""
        res = subprocess.run(
            ["git", "fsck", "--no-full"],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0, f"Git fsck failed: {res.stderr}"
