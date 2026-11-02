"""Kernel C Driver source inspector and AST/syntax analyzer."""

import re
import subprocess
from pathlib import Path
from typing import Dict, List, Optional


class DriverInspector:
    """Inspector for drivers/atrios C source code."""

    def __init__(self, drivers_dir: Path):
        self.drivers_dir = Path(drivers_dir)

    def get_source(self, filename: str) -> str:
        """Read driver C source code."""
        path = self.drivers_dir / filename
        if not path.exists():
            return ""
        return path.read_text(encoding="utf-8", errors="replace")

    def inspect_gowin_led_device(self) -> Dict[str, bool]:
        """Verify gowin_led_device.c Linux 7.0 modernizations."""
        code = self.get_source("gowin_led_device.c")
        # Check gpio set prototype
        has_int_gpio_set = bool(
            re.search(r"int\s+lp_gpio_set_value\s*\(\s*struct\s+gpio_chip", code) or
            re.search(r"int\s+\(\*set\)\s*\(struct\s+gpio_chip", code) or
            re.search(r"int\s+lp_gpio_set\s*\(", code)
        )
        # Check tty write_room
        has_uint_write_room = bool(
            re.search(r"unsigned\s+int\s+lp_tty_write_room", code) or
            re.search(r"unsigned\s+int\s+\w*write_room", code)
        )
        # Check no dev->pins direct member access
        no_pins_member_access = not bool(re.search(r"\w+->pins", code))
        # Check fb name
        has_fb_screen_name = "YNDX LED screen" in code

        return {
            "int_gpio_set": has_int_gpio_set,
            "uint_write_room": has_uint_write_room,
            "no_pins_member_access": no_pins_member_access,
            "has_fb_screen_name": has_fb_screen_name,
        }

    def inspect_gpio_keypad(self) -> Dict[str, bool]:
        """Verify gpio_keypad.c timer modernize & DTS compatibility."""
        code = self.get_source("gpio_keypad.c")
        uses_modern_timer = bool(
            "timer_container_of" in code or
            re.search(r"container_of\s*\(\s*\w+,\s*struct\s+\w+,\s*\w*timer", code)
        )
        has_no_broken_from_timer = "from_timer" not in code or uses_modern_timer
        supports_gpio_keypad_compatible = bool(
            "amlogic,gpio_keypad" in code or "gpio_keypad" in code
        )
        return {
            "uses_modern_timer": uses_modern_timer,
            "has_no_broken_from_timer": has_no_broken_from_timer,
            "supports_dts_compatible": supports_gpio_keypad_compatible,
        }

    def inspect_ltr308als01(self) -> Dict[str, bool]:
        """Verify ltr308als01.c IIO event signature modernization."""
        code = self.get_source("ltr308als01.c")
        # In Linux 6.4+, write_event_config has 'bool state'
        has_bool_event_config = bool(
            re.search(r"write_event_config\s*\([^)]*bool\s+state\)", code) or
            re.search(r"ltr308_write_event_config\s*\([^)]*bool\s+state\)", code)
        )
        return {
            "has_bool_event_config": has_bool_event_config,
        }

    def inspect_leds_is31fl32xx(self) -> Dict[str, bool]:
        """Verify leds-is31fl32xx.c of_match headers & API."""
        code = self.get_source("leds-is31fl32xx.c")
        has_of_headers = bool(
            "linux/of_device.h" in code or
            "linux/mod_devicetable.h" in code or
            "linux/of.h" in code
        )
        uses_of_device_match = bool(
            "of_match_device" in code or "of_device_get_match_data" in code
        )
        return {
            "has_of_headers": has_of_headers,
            "uses_of_match": uses_of_device_match,
        }

    def inspect_snd_es8156(self) -> Dict[str, bool]:
        """Verify snd-soc-es8156.c DAI clock master provider/consumer macros."""
        code = self.get_source("snd-soc-es8156.c")
        uses_provider_consumer = bool(
            "SND_SOC_DAIFMT_CBP_CFP" in code or "SND_SOC_DAIFMT_CBC_CFC" in code
        )
        has_no_undeclared_cbm = "SND_SOC_DAIFMT_CBM_CFM" not in code or uses_provider_consumer
        return {
            "uses_provider_consumer_macros": uses_provider_consumer,
            "has_no_undeclared_cbm": has_no_undeclared_cbm,
        }

    def inspect_snd_es7210(self) -> Dict[str, bool]:
        """Verify snd-soc-es7210.c DAI clock master provider/consumer macros."""
        code = self.get_source("snd-soc-es7210.c")
        uses_provider_consumer = bool(
            "SND_SOC_DAIFMT_CBP_CFP" in code or "SND_SOC_DAIFMT_CBC_CFC" in code
        )
        has_no_undeclared_cbm = "SND_SOC_DAIFMT_CBM_CFM" not in code or uses_provider_consumer
        return {
            "uses_provider_consumer_macros": uses_provider_consumer,
            "has_no_undeclared_cbm": has_no_undeclared_cbm,
        }

    def inspect_snd_sy6045s(self) -> Dict[str, bool]:
        """Verify snd-soc-sy6045s.c firmware path and burst I2C transfers."""
        code = self.get_source("snd-soc-sy6045s.c")
        requests_fw = "request_firmware" in code
        supports_atrios_prefix = "atrios/" in code or "sy6045s-" in code
        uses_burst_i2c = "i2c_smbus_write_i2c_block_data" in code
        return {
            "requests_fw": requests_fw,
            "supports_atrios_prefix": supports_atrios_prefix,
            "uses_burst_i2c": uses_burst_i2c,
        }

    def inspect_rotary_volume(self) -> Dict[str, bool]:
        """Verify rotary-volume.c Gray code quadrature state machine."""
        code = self.get_source("rotary-volume.c")
        has_state_machine = bool(
            re.search(r"(gray|quadrature|state_table|rotary_state|encoder_state)", code, re.IGNORECASE) or
            ("prev_state" in code and "curr_state" in code) or
            ("<< 2" in code)
        )
        no_lossy_workqueue = "schedule_work" not in code or has_state_machine
        handles_step_increment = bool(
            "KEY_VOLUMEUP" in code and "KEY_VOLUMEDOWN" in code
        )
        return {
            "has_quadrature_state_machine": has_state_machine,
            "no_lossy_workqueue": no_lossy_workqueue,
            "handles_volume_keys": handles_step_increment,
        }

    def inspect_quasar_led_ring(self) -> Dict[str, bool]:
        """Verify quasar_led_ring.c brightness math and fb name."""
        code = self.get_source("quasar_led_ring.c")
        has_correct_scaling = bool(
            re.search(r"brightness\s*\*\s*\w+\s*\/\s*255", code) or
            re.search(r"\(\s*\w+\s*\*\s*ring_priv\.brightness\s*\)\s*\/\s*255", code) or
            re.search(r"\(\s*\w+\s*\*\s*brightness\s*\)\s*\/\s*255", code)
        )
        has_yndx_ring_id = "YNDX LED ring" in code
        return {
            "has_correct_scaling": has_correct_scaling,
            "has_yndx_ring_id": has_yndx_ring_id,
        }

    def inspect_quasar_pcba_ids(self) -> Dict[str, bool]:
        """Verify quasar_pcba_ids.c discrete EEPROM property parsing."""
        code = self.get_source("quasar_pcba_ids.c")
        parses_discrete_addrs = bool(
            "top-eeprom-addr" in code or
            "main-eeprom-addr" in code or
            "led-eeprom-addr" in code
        )
        return {
            "parses_discrete_addrs": parses_discrete_addrs,
        }

    def inspect_snd_tas5825m(self) -> Dict[str, bool]:
        """Verify snd-soc-tas5825m.c regulator return check and remove cleanup."""
        code = self.get_source("snd-soc-tas5825m.c")
        checks_bulk_enable = bool(
            re.search(r"ret\s*=\s*regulator_bulk_enable", code) or
            re.search(r"if\s*\(\s*regulator_bulk_enable", code)
        )
        has_remove_disable = "regulator_bulk_disable" in code
        return {
            "checks_bulk_enable": checks_bulk_enable,
            "has_remove_disable": has_remove_disable,
        }

    def inspect_quasar_led_screen(self) -> Dict[str, bool]:
        """Verify quasar_led_screen.c clean code / no dead functions."""
        code = self.get_source("quasar_led_screen.c")
        # Functions that caused unused-function warnings in survey
        has_dead_qls_draw_lines = bool(re.search(r"static\s+void\s+qls_draw_lines", code))
        has_dead_sa_show_pic = bool(re.search(r"static\s+int\s+sa_show_pic", code))
        return {
            "no_dead_qls_draw_lines": not has_dead_qls_draw_lines,
            "no_dead_sa_show_pic": not has_dead_sa_show_pic,
        }

    def inspect_zigbee_control(self) -> Dict[str, bool]:
        """Verify zigbee-control.c compatible match and sysfs nodes."""
        code = self.get_source("zigbee-control.c")
        matches_leftymods = "leftymods,zigbee-control" in code
        matches_yandex = "yandex,zigbee-control" in code
        has_reset_attr = "zb-reset" in code or "reset" in code
        has_boot_attr = "zb-boot" in code or "boot" in code
        has_power_attr = "zb-power" in code or "power" in code
        return {
            "matches_leftymods_compatible": matches_leftymods,
            "matches_yandex_compatible": matches_yandex,
            "has_sysfs_attrs": has_reset_attr and has_boot_attr and has_power_attr,
        }
