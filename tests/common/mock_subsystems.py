"""Hardware subsystem simulation and mock drivers for opaque-box testing."""

from typing import List, Tuple, Optional


class MockRotaryEncoder:
    """Quadrature rotary encoder simulator for rotary-volume testing."""

    # Standard 2-bit Gray code transition sequence
    CW_STEPS = [(0, 0), (0, 1), (1, 1), (1, 0), (0, 0)]
    CCW_STEPS = [(0, 0), (1, 0), (1, 1), (0, 1), (0, 0)]

    def __init__(self):
        self.state = 0  # 2-bit state: (pinA << 1) | pinB
        self.volume = 50
        self.min_vol = 0
        self.max_vol = 100
        self.step_history: List[int] = []

    @staticmethod
    def gray_decode_transition(prev: int, curr: int) -> int:
        """Decode quadrature transition: +1 (CW), -1 (CCW), 0 (invalid/no change)."""
        # Lookup table for all 16 possible transitions (prev << 2 | curr)
        TRANSITION_TABLE = [
            0,  +1,  -1,   0,   # prev 00 -> 00, 01, 10, 11
            -1,  0,   0,  +1,   # prev 01 -> 00, 01, 10, 11
            +1,  0,   0,  -1,   # prev 10 -> 00, 01, 10, 11
            0,  -1,  +1,   0,   # prev 11 -> 00, 01, 10, 11
        ]
        index = ((prev & 0x03) << 2) | (curr & 0x03)
        return TRANSITION_TABLE[index]

    def step(self, pin_a: int, pin_b: int) -> int:
        """Apply new pin levels and update volume."""
        curr_state = ((pin_a & 1) << 1) | (pin_b & 1)
        direction = self.gray_decode_transition(self.state, curr_state)
        self.state = curr_state
        if direction != 0:
            self.volume = max(self.min_vol, min(self.max_vol, self.volume + direction))
            self.step_history.append(direction)
        return direction


class MockFramebufferScreen:
    """Gowin FPGA 25x16 screen framebuffer mock."""

    WIDTH = 25
    HEIGHT = 16

    def __init__(self, fb_id: str = "YNDX LED screen"):
        self.fb_id = fb_id
        self.buffer = bytearray(self.WIDTH * self.HEIGHT)

    def write_pixel(self, x: int, y: int, value: int):
        if 0 <= x < self.WIDTH and 0 <= y < self.HEIGHT:
            self.buffer[y * self.WIDTH + x] = min(255, max(0, value))

    def get_pixel(self, x: int, y: int) -> int:
        if 0 <= x < self.WIDTH and 0 <= y < self.HEIGHT:
            return self.buffer[y * self.WIDTH + x]
        return 0

    def clear(self):
        self.buffer = bytearray(self.WIDTH * self.HEIGHT)


class MockFramebufferRing:
    """Dual IS31FL3236A 72-channel LED ring framebuffer mock."""

    NUM_LEDS = 24  # 24 RGB LEDs = 72 channels
    CHANNELS = 72

    def __init__(self, fb_id: str = "YNDX LED ring"):
        self.fb_id = fb_id
        self.brightness = 255  # sysfs backlight 0..255
        self.raw_buffer = bytearray(self.CHANNELS)

    def set_backlight(self, brightness: int):
        self.brightness = max(0, min(255, brightness))

    def apply_brightness_scaling(self, raw_value: int) -> int:
        """Calculate scaled channel output: (raw * brightness) // 255."""
        return (raw_value * self.brightness) // 255

    def sync(self) -> List[int]:
        """Produce hardware register output after brightness scaling."""
        return [self.apply_brightness_scaling(val) for val in self.raw_buffer]


class MockZigbeeUART:
    """Silicon Labs EFR32 EZSP NCP UART exchange simulator."""

    def __init__(self):
        self.reset_pin = 1
        self.boot_pin = 1
        self.baudrate = 115200
        self.ezsp_connected = False

    def toggle_reset(self, duration_ms: int = 100):
        self.reset_pin = 0
        # Simulated delay
        self.reset_pin = 1
        self.ezsp_connected = True

    def send_frame(self, frame: bytes) -> bytes:
        """Respond to standard EZSP frames (0x1A 0xC0)."""
        if not self.ezsp_connected:
            return b""
        if frame.startswith(b"\x1a\xc0"):
            return b"\x1a\xc1\x01\x00"  # EZSP version reply
        return b"\x00"


class MockBluetoothA2DP:
    """BlueZ / PipeWire Bluetooth A2DP Sink state machine simulator."""

    def __init__(self):
        self.state = "disconnected"
        self.volume = 64  # AVRCP 0..127
        self.codec = "SBC"

    def connect(self, device_mac: str) -> bool:
        self.state = "connected"
        return True

    def start_stream(self) -> bool:
        if self.state == "connected":
            self.state = "streaming"
            return True
        return False

    def set_avrcp_volume(self, vol: int):
        self.volume = max(0, min(127, vol))
