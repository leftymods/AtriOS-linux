#!/usr/bin/env python3
"""Normalize only startup shell text in an offline staging root, never binaries."""
import os
from pathlib import Path
import sys

FISH_DIRS = ("etc/fish", "usr/share/fish", "root/.config/fish")
SCRIPT_FILES = ("usr/bin/atrios",)


def candidates(root):
    seen = set()
    for relative in FISH_DIRS:
        base = root / relative
        if not base.is_dir() or base.is_symlink():
            continue
        try:
            base.resolve().relative_to(root)
        except ValueError:
            continue
        for directory, dirs, names in os.walk(base, followlinks=False):
            dirs[:] = [name for name in dirs if not (Path(directory) / name).is_symlink()]
            for name in names:
                path = Path(directory) / name
                if path.suffix == ".fish" and not path.is_symlink() and path.is_file():
                    if path not in seen:
                        seen.add(path)
                        yield path, True
    for relative in SCRIPT_FILES:
        path = root / relative
        if path.is_file() and not path.is_symlink():
            try:
                path.resolve().relative_to(root)
            except ValueError:
                continue
            yield path, False


def normalize(root):
    root = Path(root).resolve(strict=True)
    if root == Path("/") or not root.is_dir():
        raise ValueError("An offline staging directory, not the running root, is required")
    changed = []
    for path, fish in candidates(root):
        before = path.read_bytes()
        # NUL/ELF/invalid UTF-8 are never passed through a text conversion.
        if b"\x00" in before or before.startswith(b"\x7fELF"):
            raise ValueError("Expected startup text, found binary: " + str(path.relative_to(root)))
        before.decode("utf-8-sig")
        if not fish and not before.startswith((b"#!", b"\xef\xbb\xbf#!")):
            raise ValueError("Expected a script shebang: " + str(path.relative_to(root)))
        after = before.removeprefix(b"\xef\xbb\xbf").replace(b"\r\n", b"\n")
        # An accidental Python/heredoc prefix was observed in root's fish config.
        # Keep all normal fish escapes and continuation lines elsewhere intact.
        if fish and after.startswith(b"\\\n"):
            after = after[2:]
        if after != before:
            path.write_bytes(after)  # Preserve owner, permissions and hard-link identity.
            changed.append(path.relative_to(root).as_posix())
    return changed


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: normalize-rootfs-text.py STAGING_ROOT")
    print("Normalized startup text files:", len(normalize(sys.argv[1])))
