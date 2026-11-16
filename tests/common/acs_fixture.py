"""Minimal structurally valid G12A ACS for tests; no hardware timings."""
import struct

def make_acs(board_id=4, dram_type=2):
    base = 0xFFFAF000
    data = bytearray(352)
    struct.pack_into("<5sBHQ", data, 0, b"acs__", 0, 1, 112)
    for offset, magic, size, address in (
        (16, b"ddrs_", 192, 112), (32, b"ddrr_", 16, 304),
        (48, b"pll__", 32, 320)):
        struct.pack_into("<5sBHQ", data, offset, magic, 1, size, base + address)
    data[112:116] = b"cfg."
    data[124] = board_id
    data[125] = 1
    data[126] = dram_type
    return bytes(data)
