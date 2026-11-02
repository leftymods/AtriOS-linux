"""Tier 1: Feature F04 - Kernel 7.0 Driver Modernization."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector


@pytest.mark.tier1
@pytest.mark.feature("F04")
@pytest.mark.milestone("M2")
class TestF04DriverModernization:
    """Verify modernization of compilation-breaking drivers for Linux 7.0."""

    @pytest.fixture(autouse=True)
    def setup_inspector(self, drivers_dir: Path):
        self.inspector = DriverInspector(drivers_dir)

    def test_f04_gowin_led_device_modernized(self):
        """Verify gowin_led_device gpio set prototype, tty write_room, and pinctrl API."""
        res = self.inspector.inspect_gowin_led_device()
        assert res["int_gpio_set"], "lp_gpio_set_value does not return int"
        assert res["uint_write_room"], "tty write_room does not return unsigned int"
        assert res["no_pins_member_access"], "Driver still accesses dev->pins directly"

    def test_f04_gpio_keypad_timer_modernized(self):
        """Verify gpio_keypad timer callback uses modern timer macros."""
        res = self.inspector.inspect_gpio_keypad()
        assert res["uses_modern_timer"], "Driver does not use timer_container_of"
        assert res["has_no_broken_from_timer"], "Driver still uses undefined from_timer macro"

    def test_f04_ltr308als01_event_signature(self):
        """Verify ltr308als01 write_event_config uses 'bool state'."""
        res = self.inspector.inspect_ltr308als01()
        assert res["has_bool_event_config"], "write_event_config signature missing 'bool state'"

    def test_f04_leds_is31fl32xx_headers(self):
        """Verify leds-is31fl32xx includes appropriate of_match headers."""
        res = self.inspector.inspect_leds_is31fl32xx()
        assert res["has_of_headers"], "Missing of_device.h or mod_devicetable.h headers"

    def test_f04_snd_codecs_daifmt_modernized(self):
        """Verify ES8156 and ES7210 codecs use provider/consumer DAI format macros."""
        res_es8156 = self.inspector.inspect_snd_es8156()
        res_es7210 = self.inspector.inspect_snd_es7210()
        assert res_es8156["uses_provider_consumer_macros"], "ES8156 does not use CBP_CFP/CBC_CFC"
        assert res_es7210["uses_provider_consumer_macros"], "ES7210 does not use CBP_CFP/CBC_CFC"
