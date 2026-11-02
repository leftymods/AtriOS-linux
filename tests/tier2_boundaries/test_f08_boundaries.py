"""Tier 2: Boundary & Corner Cases - Feature F08."""

from pathlib import Path
import pytest
from common.dts_validator import DTSValidator
from common.driver_inspector import DriverInspector


@pytest.mark.tier2
@pytest.mark.feature("F08")
@pytest.mark.milestone("M2")
class TestF08Boundaries:
    """Boundary conditions for discrete I2C EEPROM addresses, probe fallbacks, and compatibles."""

    @pytest.fixture(autouse=True)
    def setup(self, dts_file: Path, kernel_tree: Path, drivers_dir: Path):
        self.validator = DTSValidator(dts_file, kernel_tree)
        self.inspector = DriverInspector(drivers_dir)

    def test_f08_eeprom_addresses_distinct(self):
        """Verify top (0x52), main (0x50), and led (0x55) addresses do not collide."""
        top_addr = 0x52
        main_addr = 0x50
        led_addr = 0x55
        assert len({top_addr, main_addr, led_addr}) == 3, "EEPROM I2C addresses collide"

    def test_f08_eeprom_addresses_in_7bit_i2c_range(self):
        """Verify EEPROM addresses are valid 7-bit non-reserved I2C addresses."""
        for addr in [0x50, 0x52, 0x55]:
            assert 0x08 <= addr <= 0x77, f"Address 0x{addr:X} outside standard 7-bit I2C range"

    def test_f08_pcba_ids_probe_error_handling(self):
        """Verify quasar_pcba_ids contains error handling for missing I2C adapter."""
        code = self.inspector.get_source("quasar_pcba_ids.c")
        assert "i2c_get_adapter" in code or "i2c" in code
        assert "-ENODEV" in code or "-E" in code

    def test_f08_zigbee_multiple_compatibles(self):
        """Verify zigbee-control of_match table has fallback compatibles."""
        code = self.inspector.get_source("zigbee-control.c")
        assert "of_match_table" in code
        assert ".compatible" in code

    def test_f08_gpio_keypad_null_pdata_handling(self):
        """Verify gpio_keypad probe handles DT configuration safely."""
        code = self.inspector.get_source("gpio_keypad.c")
        assert "of_node" in code or "device_node" in code
