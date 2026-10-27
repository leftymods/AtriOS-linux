"""
Transparent WSL Re-exec Bridge & Path Translation Engine.
Module: builder.bridge
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from typing import List, Optional


def is_windows() -> bool:
    """Check if current operating environment is Windows."""
    return sys.platform == "win32"


def win_to_wsl_path(path: str) -> str:
    """
    Convert a Windows filesystem path to its WSL equivalent.
    Example:
        C:\\Users\\ggala\\Desktop\\testFW -> /mnt/c/Users/ggala/Desktop/testFW
        d:/Android/tools/AmlogicKitchen/bin/ampack -> /mnt/d/Android/tools/AmlogicKitchen/bin/ampack
    """
    if not path:
        return ""

    p = str(path).strip()
    # Already a Unix / WSL path
    if p.startswith("/"):
        return p

    # Handle WSL UNC paths: \\wsl$\Ubuntu-22.04\home\...
    m_unc = re.match(r"^\\\\(?:wsl\$|wsl\.localhost)\\[^\\]+(.*)", p)
    if m_unc:
        sub = m_unc.group(1).replace("\\", "/")
        return sub if sub else "/"

    # Handle Windows drive letters: C:\... or c:/...
    m = re.match(r"^([a-zA-Z]):[\\/](.*)", p)
    if m:
        drive = m.group(1).lower()
        rest = m.group(2).replace("\\", "/")
        return f"/mnt/{drive}/{rest}"

    # Handle bare drive: C:
    m_bare = re.match(r"^([a-zA-Z]):$", p)
    if m_bare:
        drive = m_bare.group(1).lower()
        return f"/mnt/{drive}"

    # Relative path with backslashes
    return p.replace("\\", "/")


def wsl_to_win_path(path: str) -> str:
    """
    Convert a WSL filesystem path to its Windows equivalent.
    Example:
        /mnt/c/Users/ggala/Desktop/testFW -> C:\\Users\\ggala\\Desktop\\testFW
        /home/user/... -> \\\\wsl$\\Ubuntu-22.04\\home\\user\\...
    """
    if not path:
        return ""

    p = str(path).strip()
    m = re.match(r"^/mnt/([a-zA-Z])(?:/(.*))?$", p)
    if m:
        drive = m.group(1).upper()
        rest = m.group(2)
        if rest:
            rest_win = rest.replace("/", "\\")
            return f"{drive}:\\{rest_win}"
        return f"{drive}:\\"

    # Already a Windows path
    if re.match(r"^[a-zA-Z]:", p):
        return p.replace("/", "\\")

    # Linux native path outside /mnt (e.g. /home/user/...)
    if p.startswith("/"):
        p_win = p.replace("/", "\\")
        return f"\\\\wsl$\\Ubuntu-22.04{p_win}"

    return p.replace("/", "\\")


def reexec_in_wsl(
    args: Optional[List[str]] = None,
    distro: str = "Ubuntu-22.04",
    user: str = "lmods",
) -> int:
    """
    Transparently delegate command execution to WSL2 Ubuntu-22.04.
    Translates script path and any path arguments, streams stdout/stderr,
    and propagates the process exit code.
    """
    if args is None:
        args = sys.argv[1:]

    script_path = os.path.abspath(sys.argv[0])
    wsl_script = win_to_wsl_path(script_path)

    # Translate path arguments if needed
    translated_args: List[str] = []
    for arg in args:
        if re.match(r"^[a-zA-Z]:[\\/]", arg) or arg.startswith("\\\\"):
            translated_args.append(win_to_wsl_path(arg))
        else:
            translated_args.append(arg)

    cmd = [
        "wsl.exe",
        "-d", distro,
        "-u", user,
        "--",
        "python3",
        wsl_script,
    ] + translated_args

    try:
        proc = subprocess.run(cmd)
        return proc.returncode
    except KeyboardInterrupt:
        return 130
    except FileNotFoundError:
        sys.stderr.write("[ERROR] wsl.exe not found on system PATH.\n")
        return 3
    except Exception as exc:
        sys.stderr.write(f"[ERROR] WSL execution failed: {exc}\n")
        return 3
