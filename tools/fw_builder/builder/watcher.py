"""
File System Watcher Daemon with 2.0-second Debounce Quiescence & Live Mount Sync.
Module: builder.watcher
"""
from __future__ import annotations

import os
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Callable, Dict, List, Optional, Set, Tuple

DEFAULT_DEBOUNCE_SECONDS = 2.0
IGNORED_EXTENSIONS: Set[str] = {
    ".tmp", ".swp", ".bak", "~", ".crswap", ".part", ".kate-swp"
}

# Tier 1 Watchdog import check
try:
    from watchdog.observers import Observer  # type: ignore
    from watchdog.events import FileSystemEventHandler  # type: ignore
    HAS_WATCHDOG = True
except ImportError:
    HAS_WATCHDOG = False


def is_temp_file(filename: str) -> bool:
    """Check if file is a temporary editor/swap file that should be ignored."""
    name = Path(filename).name
    if name.startswith(".#") or name.startswith("~") or name.startswith(".~"):
        return True
    return any(name.endswith(ext) for ext in IGNORED_EXTENSIONS)


class DebounceEngine:
    """
    2.0-second Debounce Engine.
    Accumulates file modification/addition/deletion events.
    Resets debounce timer upon each new event.
    Triggers rebuild only when continuous seconds of quiescence have passed.
    """

    def __init__(
        self,
        quiescence_seconds: float = DEFAULT_DEBOUNCE_SECONDS,
        callback: Optional[Callable[[], None]] = None,
    ):
        self.quiescence_seconds = max(0.0, float(quiescence_seconds))
        self.callback = callback
        self._last_event_time: Optional[float] = None
        self._accumulated_events: Set[str] = set()
        self._lock = threading.Lock()

    def record_event(self, filepath: str) -> None:
        """Accumulate event and reset the debounce timer."""
        with self._lock:
            self._accumulated_events.add(filepath)
            self._last_event_time = time.monotonic()

    def has_pending_events(self) -> bool:
        """Check if any events are waiting for quiescence."""
        with self._lock:
            return bool(self._accumulated_events)

    def get_pending_events(self) -> Set[str]:
        """Return copy of currently accumulated event file paths."""
        with self._lock:
            return set(self._accumulated_events)

    def check_quiescence(self, now: Optional[float] = None) -> Optional[Set[str]]:
        """
        Check if quiescence window has elapsed without new write events.
        If quiescent, clears events, resets timer, and returns the accumulated paths.
        Otherwise returns None.
        """
        with self._lock:
            if not self._accumulated_events or self._last_event_time is None:
                return None
            curr = now if now is not None else time.monotonic()
            if (curr - self._last_event_time) >= self.quiescence_seconds:
                events = set(self._accumulated_events)
                self._accumulated_events.clear()
                self._last_event_time = None
                return events
        return None

    def reset(self) -> None:
        """Reset pending events and timer."""
        with self._lock:
            self._accumulated_events.clear()
            self._last_event_time = None


def default_rebuild_callback(verbose: bool = False) -> None:
    """Default rebuild callback triggering incremental build with duration reporting."""
    print("[WATCH] Quiescence reached (2.0s debounce). Executing incremental rebuild...")
    t0 = time.perf_counter()
    try:
        from builder.repack import build_image
        code, out_img = build_image(from_scratch=False, verbose=verbose)
        duration = time.perf_counter() - t0
        if code == 0:
            print(f"[SUCCESS] Firmware image rebuilt successfully in {duration:.2f}s: {out_img}")
        else:
            print(f"[ERROR] Firmware rebuild failed in {duration:.2f}s (code: {code})")
    except Exception as exc:
        duration = time.perf_counter() - t0
        print(f"[ERROR] Rebuild exception after {duration:.2f}s: {exc}")


def _scan_directory_mtimes_and_sizes(directories: List[Path]) -> Dict[str, Tuple[float, int]]:
    """
    Scan directory trees using os.walk and return dict of path -> (mtime, size)
    for all non-temporary files.
    """
    snapshot: Dict[str, Tuple[float, int]] = {}
    for d in directories:
        if not d.exists() or not d.is_dir():
            continue
        try:
            for root, _, files in os.walk(str(d)):
                for fname in files:
                    if is_temp_file(fname):
                        continue
                    fpath = os.path.join(root, fname)
                    try:
                        st = os.stat(fpath)
                        snapshot[fpath] = (st.st_mtime, st.st_size)
                    except OSError:
                        pass
        except OSError:
            pass
    return snapshot


def _detect_changes(
    old_snapshot: Dict[str, Tuple[float, int]],
    new_snapshot: Dict[str, Tuple[float, int]],
) -> Set[str]:
    """Compare snapshots and return set of added, deleted, or modified file paths."""
    changed = set()
    old_keys = set(old_snapshot.keys())
    new_keys = set(new_snapshot.keys())

    # Added files
    changed.update(new_keys - old_keys)
    # Deleted files
    changed.update(old_keys - new_keys)
    # Modified files
    for k in old_keys & new_keys:
        if old_snapshot[k] != new_snapshot[k]:
            changed.add(k)

    return changed


def resolve_watch_directories(watch_dirs: Optional[List[str]] = None) -> List[Path]:
    """
    Resolve target watch directories.
    Defaults to overlay/vendor, overlay/dts, overlay/boot,
    plus active loop mount directories (e.g. <project_root>/mnt_vendor/).
    """
    project_root = Path(__file__).resolve().parent.parent

    if watch_dirs is not None:
        return [Path(p) for p in watch_dirs]

    default_paths = [
        project_root / "overlay" / "vendor",
        project_root / "overlay" / "dts",
        project_root / "overlay" / "boot",
    ]

    # Include active loop mounts
    try:
        from builder.mount import list_mounted_partitions
        mounted = list_mounted_partitions()
        for mnt_path in mounted.values():
            p_mnt = Path(mnt_path)
            if p_mnt.exists() and p_mnt not in default_paths:
                default_paths.append(p_mnt)
    except Exception:
        pass

    return default_paths


def _sync_live_mounts_if_needed(changed_paths: Set[str]) -> None:
    """If any changed paths reside in a loop-mounted partition directory, run sudo sync."""
    mount_prefixes: List[str] = []
    try:
        from builder.mount import list_mounted_partitions
        for m in list_mounted_partitions().values():
            mount_prefixes.append(os.path.abspath(m))
    except Exception:
        pass

    needs_sync = False
    for path in changed_paths:
        abs_p = os.path.abspath(path)
        if "mnt_" in abs_p:
            needs_sync = True
            break
        for mp in mount_prefixes:
            if abs_p.startswith(mp):
                needs_sync = True
                break
        if needs_sync:
            break

    if needs_sync and os.name == "posix":
        try:
            subprocess.run(["sudo", "sync"], capture_output=True, check=False)
        except Exception:
            pass


if HAS_WATCHDOG:
    class _WatchdogHandler(FileSystemEventHandler):
        """Watchdog event handler routing filesystem events to the DebounceEngine."""

        def __init__(self, engine: DebounceEngine):
            super().__init__()
            self.engine = engine

        def on_any_event(self, event):
            if event.is_directory:
                return
            src = getattr(event, "src_path", "")
            if src and not is_temp_file(src):
                self.engine.record_event(src)


def start_watcher(
    watch_dirs: Optional[List[str]] = None,
    debounce_seconds: float = DEFAULT_DEBOUNCE_SECONDS,
    poll_interval: float = 0.5,
    callback: Optional[Callable[[], None]] = None,
    max_iterations: Optional[int] = None,
    stop_event: Optional[threading.Event] = None,
    verbose: bool = False,
) -> None:
    """
    Two-tier monitoring daemon:
    - Attempts to use watchdog if available.
    - Seamlessly falls back to stdlib polling loop (os.walk checking mtime and size).
    - 2.0s Debounce Engine: resets debounce timer on each event, triggers rebuild callback
      only when 2.0s continuous quiescence is reached.
    - Live Mount Sync: executes sudo sync before rebuild if edit occurred in a mounted partition.
    """
    debounce_sec = max(0.0, float(debounce_seconds))
    poll_sec = max(0.01, float(poll_interval))

    # Boundary check: empty list
    if watch_dirs is not None and len(watch_dirs) == 0:
        return

    # Check existence if explicit list was provided
    if watch_dirs is not None:
        for d in watch_dirs:
            p = Path(d)
            if not p.exists():
                raise FileNotFoundError(f"Watch directory does not exist: {d}")

    target_paths = resolve_watch_directories(watch_dirs)
    valid_dirs = [p for p in target_paths if p.exists() and p.is_dir()]
    if not valid_dirs:
        return

    cb = callback or (lambda: default_rebuild_callback(verbose=verbose))
    engine = DebounceEngine(quiescence_seconds=debounce_sec, callback=cb)

    # Tier 1: Try watchdog
    observer: Optional[Any] = None
    if HAS_WATCHDOG:
        try:
            observer = Observer()
            handler = _WatchdogHandler(engine)
            for vd in valid_dirs:
                observer.schedule(handler, str(vd), recursive=True)
            observer.start()
        except Exception as exc:
            observer = None

    # Tier 2: Stdlib polling loop
    last_snapshot = _scan_directory_mtimes_and_sizes(valid_dirs)
    iterations = 0

    try:
        while True:
            if stop_event is not None and stop_event.is_set():
                break
            if max_iterations is not None and iterations >= max_iterations:
                break
            iterations += 1

            time.sleep(poll_sec)

            # Polling check if watchdog is not active
            if observer is None:
                current_snapshot = _scan_directory_mtimes_and_sizes(valid_dirs)
                changed = _detect_changes(last_snapshot, current_snapshot)
                for fpath in changed:
                    engine.record_event(fpath)
                last_snapshot = current_snapshot

            # Check for quiescence
            quiescent_events = engine.check_quiescence()
            if quiescent_events is not None:
                _sync_live_mounts_if_needed(quiescent_events)
                try:
                    cb()
                except Exception as exc:
                    print(f"[WATCHER] Callback error: {exc}")

    except KeyboardInterrupt:
        pass
    finally:
        if observer is not None:
            try:
                observer.stop()
                observer.join(timeout=1.0)
            except Exception:
                pass
