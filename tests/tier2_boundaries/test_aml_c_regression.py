"""Production C ABI/checksum regression, independent of the Python model."""
import os
import shutil
import subprocess
from pathlib import Path
import pytest
from common.acs_fixture import make_acs
from common.aml_burn_inspector import AcsBinParser

@pytest.mark.tier2
@pytest.mark.aml_burn
def test_production_c_wire_and_checksum(tmp_path):
    compiler = shutil.which("cc")
    if not compiler:
        pytest.skip("host C compiler unavailable")
    root = Path(__file__).resolve().parents[2]
    include = tmp_path / "linux"
    include.mkdir()
    (include / "types.h").write_text("#include <stdint.h>\ntypedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef uint32_t __le32; typedef uint16_t __le16;\n#define __packed __attribute__((packed))\n")
    binary = tmp_path / "aml-wire"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(tmp_path), "-I", str(root / "board/yandex/station2/mainline/aml_burn"),
                    str(root / "tests/native/test_aml_protocol.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"})

@pytest.mark.tier2
@pytest.mark.aml_burn
def test_acs_rejects_markers_and_wrong_board():
    assert not AcsBinParser.verify_lpddr4_support(b"acs__\x02")[0]
    assert not AcsBinParser.verify_lpddr4_support(make_acs(board_id=3))[0]
    assert not AcsBinParser.verify_lpddr4_support(make_acs(dram_type=1))[0]
    assert AcsBinParser.verify_lpddr4_support(make_acs())[0]
    assert AcsBinParser.verify_lpddr4_support(make_acs(board_id=255))[0]
    data = bytearray(make_acs())
    data[24:32] = (0xffffffffffffffff).to_bytes(8, "little")
    assert not AcsBinParser.verify_lpddr4_support(data)[0]
