#!/bin/bash
# Verify the selected board's kernel profile and runtime inputs. No hardware writes.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
selected_board="${BOARD:-yndxsttion53}"
profile_only=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --board) [ "$#" -ge 2 ] || { echo '--board requires a board ID' >&2; exit 2; }; selected_board="$2"; shift 2 ;;
        --profile-only) profile_only=1; shift ;;
        *) echo "Unknown verification argument: $1" >&2; exit 2 ;;
    esac
done
source "$PROJECT_ROOT/lib/general.sh"
source "$PROJECT_ROOT/lib/config.sh"
source "$PROJECT_ROOT/lib/compilation-kernel.sh"
load_board_config "$selected_board"
resolve_kernel_build_profile
KDIR="$KERNEL_BUILD_DIR"
DTS_PATH="$KERNEL_BUILD_DTS_PATH"
DRV_DIR="$KDIR/drivers/atrios"
OVERLAY="$PROJECT_ROOT/${ROOTFS_OVERLAY:?ROOTFS_OVERLAY must be configured}"
if [ "$profile_only" = 1 ]; then
    printf 'KERNEL_BUILD_DIR=%s\nKERNEL_ARCH=%s\nDTS_PATH=%s\nDTS_SUBDIR=%s\n' \
        "$KDIR" "$KERNEL_BUILD_ARCH" "$DTS_PATH" "$KERNEL_BUILD_DTS_SUBDIR"
    exit 0
fi
PASS_COUNT=0
FAIL_COUNT=0
check_step() {
    local name="$1"
    shift
    printf '[CHECK] %s... ' "$name"
    if "$@"; then
        echo PASS
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo FAIL
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}
VERIFY_TMP=$(mktemp -d "${TMPDIR:-/tmp}/atrios-verify.XXXXXXXX")
DTB_OUT="$VERIFY_TMP/board.dtb"
DTS_PREPROCESSED="$VERIFY_TMP/board.dts"
DTS_DECOMPILED="$VERIFY_TMP/decompiled.dts"
trap 'rm -f "$DTB_OUT" "$DTS_PREPROCESSED" "$DTS_DECOMPILED"; rmdir "$VERIFY_TMP" 2>/dev/null || true' EXIT
printf 'AtriOS verification: %s (%s)\nKernel profile: %s\n' "$BOARD_NAME" "$BOARD" "$KDIR"
check_step "Cross-compiler ${CROSS_COMPILE}gcc" command -v "${CROSS_COMPILE}gcc"
check_step 'Device tree compiler' command -v dtc
check_step 'Python interpreter' python3 --version
check_step 'Selected kernel profile is prepared' test -f "$KDIR/.atrios-kernel-profile.json"
check_step 'Selected DTS exists' test -s "$DTS_PATH"
verify_dts_compile() {
    "${CROSS_COMPILE}gcc" -E -nostdinc -I"$KDIR/include" \
        -I"$KDIR/arch/$KERNEL_BUILD_ARCH/boot/dts" \
        -I"$KDIR/arch/$KERNEL_BUILD_ARCH/boot/dts/$KERNEL_BUILD_DTS_SUBDIR" \
        -undef -D__DTS__ -x assembler-with-cpp "$DTS_PATH" -o "$DTS_PREPROCESSED" || return
    dtc -I dts -O dtb -o "$DTB_OUT" "$DTS_PREPROCESSED" || return
    test -s "$DTB_OUT"
}
check_step 'Selected DTS preprocessing and compilation' verify_dts_compile
verify_dts_nodes() {
    dtc -I dtb -O dts "$DTB_OUT" > "$DTS_DECOMPILED" || return
    if [ "$DTS_NAME" = meson-sm1-yandex-station-max ]; then
        grep -q 'realtek,rtl8822cs-bt' "$DTS_DECOMPILED" &&
        grep -q 'amlogic,axg-sound-card' "$DTS_DECOMPILED" &&
        grep -q 'pdm' "$DTS_DECOMPILED" &&
        grep -q 'leftymods,station-max' "$DTS_DECOMPILED"
    else
        grep -q 'compatible =' "$DTS_DECOMPILED"
    fi
}
check_step 'Selected DTB compatibility and applicable hardware nodes' verify_dts_nodes
verify_driver_build() {
    local driver="$1" config="$2"
    make -C "$KDIR" M="$DRV_DIR" ARCH="$KERNEL_BUILD_ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
        "${config}=m" modules || return
    test -s "$DRV_DIR/${driver}.ko"
}
# These physical expectations belong to this reviewed Station profile only.
if [ "$DTS_NAME" = meson-sm1-yandex-station-max ]; then
    while read -r driver config; do
        check_step "Driver: $driver" verify_driver_build "$driver" "$config"
    done <<'DRIVERS'
gowin_led_device CONFIG_YANDEX_GOWIN_LED_DEVICE
quasar_led_screen CONFIG_YANDEX_QUASAR_LED_SCREEN
quasar_led_ring CONFIG_YANDEX_QUASAR_LED_RING
leds-is31fl32xx CONFIG_YANDEX_LEDS_IS31FL32XX
rotary-volume CONFIG_YANDEX_ROTARY_VOLUME
gpio_keypad CONFIG_YANDEX_GPIO_KEYPAD
zigbee-control CONFIG_YANDEX_ZIGBEE_CONTROL
ltr308als01 CONFIG_YANDEX_LTR308ALS01
quasar_pcba_ids CONFIG_YANDEX_QUASAR_PCBA_IDS
snd-soc-sy6045s CONFIG_YANDEX_SND_SOC_SY6045S
snd-soc-es8156 CONFIG_YANDEX_SND_SOC_ES8156
snd-soc-es7210 CONFIG_YANDEX_SND_SOC_ES7210
snd-soc-tas5825m CONFIG_YANDEX_SND_SOC_TAS5825M
DRIVERS
    FW_ROOT="$OVERLAY/lib/firmware"
    for name in sy6045s-woofer-settings.txt sy6045s-tweeters-settings.txt rtlbt/rtl8822cs_fw.bin rtlbt/rtl8822cs_config.bin; do
        check_step "Firmware: $name" test -s "$FW_ROOT/$name"
    done
else
    check_step 'Selected profile custom modules' make -C "$KDIR" M="$DRV_DIR" \
        ARCH="$KERNEL_BUILD_ARCH" CROSS_COMPILE="$CROSS_COMPILE" modules
fi
verify_runtime_syntax() {
    python3 - "$OVERLAY" <<'PYTHON'
import ast
from pathlib import Path
import subprocess
import sys
root = Path(sys.argv[1])
for relative in ['usr/lib/atrios', 'usr/bin', 'etc/init.d']:
    directory = root / relative
    if not directory.is_dir():
        continue
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.is_symlink():
            continue
        data = path.read_bytes()
        first = data.split(b'\n', 1)[0]
        if path.suffix == '.py' or (first.startswith(b'#!') and b'python' in first):
            ast.parse(data.decode('utf-8-sig'), filename=str(path))
        elif first.startswith(b'#!') and any(token.rsplit(b'/', 1)[-1] in (b'sh', b'bash', b'dash') for token in first.split()):
            subprocess.run(['bash' if b'bash' in first else 'sh', '-n', str(path)], check=True)
PYTHON
}
check_step 'Selected overlay runtime syntax' verify_runtime_syntax
printf 'Verification complete: %s passed, %s failed\n' "$PASS_COUNT" "$FAIL_COUNT"
[ "$FAIL_COUNT" -eq 0 ]
