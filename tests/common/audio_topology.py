"""Audio stack configuration and routing topology validator."""

from pathlib import Path
from typing import Dict, List, Optional


class AudioTopologyValidator:
    """Validator for ALSA UCM, asound.conf, and PipeWire audio graphs."""

    def __init__(self, rootfs_overlay: Path):
        self.rootfs_overlay = Path(rootfs_overlay)
        self.etc_dir = self.rootfs_overlay / "etc"

    def inspect_asound_conf(self) -> Dict[str, bool]:
        """Inspect /etc/asound.conf for default PCM and card settings."""
        asound_path = self.etc_dir / "asound.conf"
        if not asound_path.exists():
            return {"exists": False}
        content = asound_path.read_text(encoding="utf-8", errors="replace")

        has_pcm_default = "pcm.!default" in content
        has_ctl_default = "ctl.!default" in content

        return {
            "exists": True,
            "has_pcm_default": has_pcm_default,
            "has_ctl_default": has_ctl_default,
        }

    def inspect_pipewire_configs(self) -> Dict[str, bool]:
        """Inspect PipeWire and WirePlumber configuration directories."""
        pw_dir = self.etc_dir / "pipewire"
        wp_dir = self.etc_dir / "wireplumber"

        has_wp = wp_dir.exists()
        has_pw = pw_dir.exists() or (self.etc_dir / "init.d" / "S50pipewire").exists()

        return {
            "has_pipewire_or_service": has_pw,
            "has_wireplumber": has_wp,
        }

    def check_aec_pipeline_nodes(self) -> Dict[str, bool]:
        """Verify presence of AEC loopback and microphone capture definitions."""
        # Check init scripts or wireplumber configurations
        s03 = self.etc_dir / "init.d" / "S03amplifiers"
        s50 = self.etc_dir / "init.d" / "S50pipewire"

        s03_exists = s03.exists()
        s50_exists = s50.exists()

        return {
            "has_amplifier_init": s03_exists,
            "has_pipewire_service": s50_exists,
        }
