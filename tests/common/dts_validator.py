"""Device Tree Source (DTS) parser and validation utility."""

import re
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Set


class DTSValidator:
    """Validator for Amlogic Meson SM1 Mainline Device Tree."""

    def __init__(self, dts_path: Path, kernel_tree: Optional[Path] = None):
        self.dts_path = Path(dts_path)
        self.kernel_tree = Path(kernel_tree) if kernel_tree else Path("/home/lmods/linux-leftymods")
        self.content = self._read_dts()

    def _read_dts(self) -> str:
        if not self.dts_path.exists():
            return ""
        return self.dts_path.read_text(encoding="utf-8", errors="replace")

    def has_node(self, node_pattern: str) -> bool:
        """Check if a node pattern exists in DTS."""
        return bool(re.search(node_pattern, self.content))

    def has_property(self, prop_pattern: str) -> bool:
        """Check if a property pattern exists."""
        return bool(re.search(prop_pattern, self.content))

    def find_all(self, pattern: str) -> List[str]:
        """Find all matching occurrences."""
        return re.findall(pattern, self.content)

    def check_hdmi_connector(self) -> Dict[str, bool]:
        """Verify HDMI connector node and port endpoint."""
        return {
            "node_present": bool(re.search(r"hdmi_connector:\s*connector\s*\{", self.content) or
                                re.search(r"hdmi-connector\s*\{", self.content)),
            "compatible_hdmi": "hdmi-connector" in self.content,
            "port_endpoint_linked": "hdmi_connector_in" in self.content or "connector_in" in self.content,
        }

    def check_emmc_pwrseq(self) -> Dict[str, bool]:
        """Verify eMMC pwrseq node and mmc node reference."""
        return {
            "pwrseq_node": bool(re.search(r"emmc_pwrseq:\s*pwrseq\s*\{", self.content) or
                               re.search(r"mmc-pwrseq-emmc", self.content)),
            "mmc_reference": "mmc-pwrseq = <&emmc_pwrseq>" in self.content or "mmc-pwrseq = <&pwrseq" in self.content,
        }

    def check_emmc_pinctrl(self) -> Dict[str, bool]:
        """Verify mainline eMMC pinctrl phandles (emmc_ctrl_pins, emmc_data_8b_pins)."""
        has_correct_pins = ("emmc_ctrl_pins" in self.content and "emmc_data_8b_pins" in self.content) or \
                           ("emmc_pins" not in self.content and "emmc_ctrl" in self.content)
        has_broken_emmc_pins = bool(re.search(r"pinctrl-0\s*=\s*<&emmc_pins>", self.content))
        return {
            "has_correct_pins": has_correct_pins,
            "has_no_broken_pins": not has_broken_emmc_pins,
        }

    def check_uart_ao_b_pinctrl(self) -> Dict[str, bool]:
        """Verify mainline uart_ao_b pinctrl phandle (uart_ao_b_pins vs uart_ao_b_pins_x)."""
        has_broken_reference = bool(re.search(r"pinctrl-0\s*=\s*<&uart_ao_b_pins>", self.content))
        has_valid_pins = "uart_ao_b" in self.content
        return {
            "has_no_broken_reference": not has_broken_reference,
            "valid_pins_referenced": has_valid_pins,
        }

    def check_audio_pdm(self) -> Dict[str, bool]:
        """Verify 8-channel PDM microphone bindings."""
        return {
            "pdm_node_referenced": bool(re.search(r"&pdm\s*\{", self.content) or "&pdm" in self.content),
            "dmics_codec_present": "dmic-codec" in self.content or "dmic_codec" in self.content,
            "status_okay": bool(re.search(r"&pdm\s*\{[^}]*status\s*=\s*\"okay\"", self.content)),
        }

    def check_audio_loopback_and_fifos(self) -> Dict[str, bool]:
        """Verify ES7210 AEC loopback and AXG frontend FIFO DAIs."""
        has_broken_tdmb = bool(re.search(r"sound-dai\s*=\s*<&tdmb>", self.content))
        has_broken_tdmc = bool(re.search(r"sound-dai\s*=\s*<&tdmc>", self.content))
        has_tdmif_b = "&tdmif_b" in self.content or "tdmif_b" in self.content
        has_tdmif_c = "&tdmif_c" in self.content or "tdmif_c" in self.content
        has_rx_mask = bool(re.search(r"dai-tdm-slot-rx-mask-0\s*=\s*<1\s+1>", self.content))
        return {
            "no_broken_tdmb": not has_broken_tdmb,
            "no_broken_tdmc": not has_broken_tdmc,
            "uses_tdmif_b": has_tdmif_b,
            "uses_tdmif_c": has_tdmif_c,
            "has_es7210_rx_mask": has_rx_mask,
        }

    def check_eeprom_addresses(self) -> Dict[str, bool]:
        """Verify discrete top, main, and led EEPROM I2C addresses."""
        return {
            "has_top_eeprom": "top-eeprom-addr" in self.content or "0x52" in self.content,
            "has_main_eeprom": "main-eeprom-addr" in self.content or "0x50" in self.content,
            "has_led_eeprom": "led-eeprom-addr" in self.content or "0x55" in self.content,
        }

    def compile_dtb(self) -> subprocess.CompletedProcess:
        """Run cpp + dtc compilation to verify AST and phandles resolution."""
        includes = [
            f"-I{self.kernel_tree}/arch/arm64/boot/dts/amlogic",
            f"-I{self.kernel_tree}/include",
            f"-I{self.kernel_tree}/scripts/dtc/include-prefixes",
        ]
        cmd = (
            f"cpp -nostdinc {' '.join(includes)} -undef -x assembler-with-cpp {self.dts_path} "
            f"| dtc -I dts -O dtb -o /dev/null"
        )
        return subprocess.run(cmd, shell=True, capture_output=True, text=True)
