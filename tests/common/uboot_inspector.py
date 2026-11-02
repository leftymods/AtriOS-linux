"""U-Boot defconfig and FIP signing inspector."""

import re
from pathlib import Path
from typing import Dict, List, Optional


class UBootInspector:
    """Inspector for Mainline U-Boot v2025.01 defconfig and FIP toolchain."""

    def __init__(self, repo_root: Path):
        self.repo_root = Path(repo_root)
        self.defconfig_path = (
            self.repo_root / "board" / "yandex" / "station2" / "mainline" / "uboot_station_max_defconfig"
        )
        self.script_path = self.repo_root / "scripts" / "build_mainline_uboot.sh"
        self.fip_dir = self.repo_root / "bootloader" / "uboot-repo"

    def inspect_defconfig(self) -> Dict[str, bool]:
        """Inspect U-Boot defconfig options."""
        if not self.defconfig_path.exists():
            return {}
        content = self.defconfig_path.read_text(encoding="utf-8", errors="replace")

        has_prompt = 'CONFIG_SYS_PROMPT="AtriOS-aml# "' in content or 'CONFIG_SYS_PROMPT=' in content
        has_ident = 'CONFIG_IDENT_STRING=" AtriOS-aml (by leftymods)"' in content or 'leftymods' in content
        has_fastboot = "CONFIG_CMD_FASTBOOT=y" in content
        has_dfu = "CONFIG_CMD_DFU=y" in content
        has_manufacturer = 'CONFIG_USB_GADGET_MANUFACTURER="leftymods"' in content
        has_aml_burn = "aml_burn" in content

        return {
            "has_atrios_prompt": has_prompt,
            "has_leftymods_ident": has_ident,
            "has_cmd_fastboot": has_fastboot,
            "has_cmd_dfu": has_dfu,
            "has_manufacturer_leftymods": has_manufacturer,
            "has_aml_burn_bootcmd": has_aml_burn,
        }

    def inspect_build_script(self) -> Dict[str, bool]:
        """Inspect scripts/build_mainline_uboot.sh for U-Boot version and FIP steps."""
        if not self.script_path.exists():
            return {}
        content = self.script_path.read_text(encoding="utf-8", errors="replace")

        targets_v2025_01 = "v2025.01" in content or "2025.01" in content
        calls_aml_encrypt = "aml_encrypt_g12a" in content
        references_bl2 = "bl2.bin" in content
        references_bl30 = "bl30.bin" in content
        references_bl31 = "bl31.bin" in content

        return {
            "targets_v2025_01": targets_v2025_01,
            "calls_aml_encrypt": calls_aml_encrypt,
            "references_bl2": references_bl2,
            "references_bl30": references_bl30,
            "references_bl31": references_bl31,
        }

    def inspect_fip_binaries(self) -> Dict[str, bool]:
        """Verify presence of prebuilt blobs for secure FIP assembly."""
        encrypt_tool = (
            self.fip_dir / "fip" / "g12a" / "aml_encrypt_g12a"
        )
        has_encrypt_tool = encrypt_tool.exists()

        bl2_blob = self.fip_dir / "bl2" / "bin" / "g12a" / "bl2.bin"
        has_bl2 = bl2_blob.exists() or list(self.fip_dir.glob("**/bl2.bin"))

        bl30_blob = self.fip_dir / "bl30" / "bin" / "g12a" / "bl30.bin"
        has_bl30 = bl30_blob.exists() or list(self.fip_dir.glob("**/bl30.bin"))

        bl31_blob = self.fip_dir / "bl31" / "bin" / "g12a" / "bl31.img"
        has_bl31 = bl31_blob.exists() or list(self.fip_dir.glob("**/bl31*"))

        return {
            "has_encrypt_tool": has_encrypt_tool,
            "has_bl2_blob": bool(has_bl2),
            "has_bl30_blob": bool(has_bl30),
            "has_bl31_blob": bool(has_bl31),
        }
