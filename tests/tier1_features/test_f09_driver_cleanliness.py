"""Tier 1: Feature F09 - Driver Cleanliness & Error Handling."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector


@pytest.mark.tier1
@pytest.mark.feature("F09")
@pytest.mark.milestone("M2")
class TestF09DriverCleanliness:
    """Verify elimination of compiler warnings and proper resource teardown in drivers."""

    @pytest.fixture(autouse=True)
    def setup(self, drivers_dir: Path):
        self.inspector = DriverInspector(drivers_dir)
        self.drivers_dir = drivers_dir

    def test_f09_tas5825m_regulator_return_checked(self):
        """Verify tas5825m checks return value of regulator_bulk_enable."""
        res = self.inspector.inspect_snd_tas5825m()
        assert res["checks_bulk_enable"], "regulator_bulk_enable return value ignored (warn_unused_result)"

    def test_f09_tas5825m_remove_disables_regulators(self):
        """Verify tas5825m disables regulators on module remove."""
        res = self.inspector.inspect_snd_tas5825m()
        assert res["has_remove_disable"], "tas5825m_remove does not disable regulators"

    def test_f09_quasar_led_screen_no_unused_draw_lines(self):
        """Verify unused function qls_draw_lines is eliminated from quasar_led_screen.c."""
        res = self.inspector.inspect_quasar_led_screen()
        assert res["no_dead_qls_draw_lines"], "Unused static function qls_draw_lines present"

    def test_f09_quasar_led_screen_no_unused_show_pic(self):
        """Verify unused function sa_show_pic is eliminated from quasar_led_screen.c."""
        res = self.inspector.inspect_quasar_led_screen()
        assert res["no_dead_sa_show_pic"], "Unused static function sa_show_pic present"

    def test_f09_drivers_makefile_all_targets(self):
        """Verify drivers/atrios/Makefile lists all 13 kernel module targets."""
        makefile = self.drivers_dir / "Makefile"
        assert makefile.exists(), "drivers/atrios/Makefile missing"
        content = makefile.read_text(encoding="utf-8", errors="replace")
        required_objs = [
            "gowin_led_device", "quasar_led_screen", "quasar_led_ring",
            "quasar_pcba_ids", "rotary-volume", "gpio_keypad", "zigbee-control",
            "ltr308als01", "leds-is31fl32xx", "snd-soc-sy6045s", "snd-soc-es8156",
            "snd-soc-es7210", "snd-soc-tas5825m"
        ]
        for obj in required_objs:
            assert f"{obj}.o" in content, f"Module '{obj}.o' missing from drivers/atrios/Makefile"
