"""Tier 1: Feature F08 - PCBA IDs & Driver Compatible Reconciliation."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector
from common.dts_validator import DTSValidator


@pytest.mark.tier1
@pytest.mark.feature("F08")
@pytest.mark.milestone("M2")
class TestF08PCBAIDs:
    """Verify PCBA ID discrete EEPROM address handling and DTS compatible reconciliation."""

    @pytest.fixture(autouse=True)
    def setup(self, drivers_dir: Path, dts_file: Path, kernel_tree: Path):
        self.inspector = DriverInspector(drivers_dir)
        self.dts_val = DTSValidator(dts_file, kernel_tree)

    def test_f08_pcba_ids_parses_discrete_addrs(self):
        """Verify quasar_pcba_ids parses top-eeprom-addr, main-eeprom-addr, led-eeprom-addr."""
        res = self.inspector.inspect_quasar_pcba_ids()
        assert res["parses_discrete_addrs"], "quasar_pcba_ids.c does not support discrete EEPROM DT properties"

    def test_f08_dts_has_eeprom_addresses(self):
        """Verify DTS provides discrete top, main, and led EEPROM I2C addresses."""
        res = self.dts_val.check_eeprom_addresses()
        assert res["has_top_eeprom"], "DTS missing top-eeprom-addr"
        assert res["has_main_eeprom"], "DTS missing main-eeprom-addr"
        assert res["has_led_eeprom"], "DTS missing led-eeprom-addr"

    def test_f08_zigbee_compatible_reconciliation(self):
        """Verify zigbee-control supports 'leftymods,zigbee-control' as declared in DTS."""
        res = self.inspector.inspect_zigbee_control()
        assert res["matches_leftymods_compatible"], "zigbee-control lacks 'leftymods,zigbee-control' in of_match_table"

    def test_f08_gpio_keypad_compatible_reconciliation(self):
        """Verify gpio_keypad of_match_table matches DTS compatible string."""
        res = self.inspector.inspect_gpio_keypad()
        assert res["supports_dts_compatible"], "gpio_keypad does not match DTS compatible string"

    def test_f08_zigbee_sysfs_controls_present(self):
        """Verify zigbee-control declares zb-reset, zb-boot, and zb-power sysfs nodes."""
        res = self.inspector.inspect_zigbee_control()
        assert res["has_sysfs_attrs"], "Missing sysfs control attributes in zigbee-control.c"
