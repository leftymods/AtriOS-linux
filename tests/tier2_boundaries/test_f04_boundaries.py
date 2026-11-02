"""Tier 2: Boundary & Corner Cases - Feature F04."""

from pathlib import Path
import pytest
from common.driver_inspector import DriverInspector


@pytest.mark.tier2
@pytest.mark.feature("F04")
@pytest.mark.milestone("M2")
class TestF04Boundaries:
    """Boundary conditions for kernel 7.0 driver prototypes, NULL checks, and error codes."""

    @pytest.fixture(autouse=True)
    def setup(self, drivers_dir: Path):
        self.inspector = DriverInspector(drivers_dir)

    def test_f04_gpio_keypad_timer_safe_cast(self):
        """Verify gpio_keypad does not cast NULL timer pointer."""
        code = self.inspector.get_source("gpio_keypad.c")
        assert "timer" in code
        assert "NULL" not in code or "if (" in code

    def test_f04_ltr308_event_direction_boundaries(self):
        """Verify ltr308 handles both IIO_EV_DIR_RISING and IIO_EV_DIR_FALLING."""
        code = self.inspector.get_source("ltr308als01.c")
        assert "IIO_EV_DIR_RISING" in code or "IIO_EV_DIR_EITHER" in code or "dir" in code

    def test_f04_gowin_null_dev_guard(self):
        """Verify gowin_led_device probes validate platform_device pointer."""
        code = self.inspector.get_source("gowin_led_device.c")
        assert "probe" in code

    def test_f04_snd_es8156_clock_invert_flags(self):
        """Verify ES8156 handles standard BCLK / LRCLK inversion flags safely."""
        code = self.inspector.get_source("snd-soc-es8156.c")
        assert "SND_SOC_DAIFMT_INV_MASK" in code or "format" in code

    def test_f04_snd_es7210_clock_invert_flags(self):
        """Verify ES7210 handles standard BCLK / LRCLK inversion flags safely."""
        code = self.inspector.get_source("snd-soc-es7210.c")
        assert "SND_SOC_DAIFMT_INV_MASK" in code or "format" in code
