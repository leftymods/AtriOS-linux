#!/bin/bash
# ==============================================================================
# AtriOS Mainline Verification Suite (verify_all.sh)
# Verifies toolchain, DTS compilation, 13 kernel drivers, firmware assets,
# userspace daemons, and system integration for Yandex Station Max (SM1)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0

check_step() {
    local name="$1"
    shift
    echo -e "${CYAN}[CHECK]${NC} $name..."
    if "$@"; then
        echo -e "       ${GREEN}PASS${NC}"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "       ${RED}FAIL${NC}"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

echo "=========================================================="
echo "    AtriOS Mainline Verification Suite - Station Max      "
echo "=========================================================="

# ------------------------------------------------------------------------------
# Tier 1: Toolchain & Build Dependencies
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}--- Tier 1: Toolchain & Build Dependencies ---${NC}"

check_step "Cross-compiler aarch64-linux-gnu-gcc" which aarch64-linux-gnu-gcc
check_step "Device Tree Compiler (dtc)" which dtc
check_step "Python 3 interpreter" python3 --version

# ------------------------------------------------------------------------------
# Tier 2: Device Tree Syntax & Binary Compilation
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}--- Tier 2: Device Tree Verification ---${NC}"

DTS_PATH="$PROJECT_ROOT/board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts"
DTB_OUT="/tmp/test_verify_max.dtb"

check_step "DTS file existence" test -f "$DTS_PATH"

verify_dts_compile() {
    local kdir="/home/lmods/linux-7.3.1"
    if [ ! -d "$kdir" ]; then
        kdir="$PROJECT_ROOT/cache/kernel"
    fi
    aarch64-linux-gnu-gcc -E -nostdinc -I"$kdir/include" -I"$kdir/arch/arm64/boot/dts" -I"$kdir/arch/arm64/boot/dts/amlogic" \
        -undef -D__DTS__ -x assembler-with-cpp "$DTS_PATH" -o /tmp/verify.dts
    dtc -I dts -O dtb -o "$DTB_OUT" /tmp/verify.dts 2>/dev/null
    test -s "$DTB_OUT"
}
check_step "DTS preprocessing & dtc compilation" verify_dts_compile

verify_dts_nodes() {
    dtc -I dtb -O dts "$DTB_OUT" 2>/dev/null > /tmp/verify_decomp.dts
    grep -q "realtek,rtl8822cs-bt" /tmp/verify_decomp.dts && \
    grep -q "amlogic,axg-sound-card" /tmp/verify_decomp.dts && \
    grep -q "pdm" /tmp/verify_decomp.dts && \
    grep -q "leftymods,station-max" /tmp/verify_decomp.dts
}
check_step "DTS critical nodes (BT serdev, PDM, Sound card, SoC model)" verify_dts_nodes

# ------------------------------------------------------------------------------
# Tier 3: Kernel Drivers Compilation (13 AtriOS Hardware Drivers)
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}--- Tier 3: 13 Hardware Drivers Compilation ---${NC}"

DRV_DIR="$PROJECT_ROOT/drivers/atrios"
KDIR="/home/lmods/linux-7.3.1"
[ ! -d "$KDIR" ] && KDIR="$PROJECT_ROOT/cache/kernel"

verify_driver_build() {
    local drv="$1"
    local cfg="$2"
    make -C "$KDIR" M="$DRV_DIR" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- "${cfg}=m" modules >/dev/null 2>&1
    test -f "$DRV_DIR/${drv}.ko"
}

check_step "Driver: gowin_led_device (Gowin FPGA 25x16 screen)" verify_driver_build "gowin_led_device" "CONFIG_YANDEX_GOWIN_LED_DEVICE"
check_step "Driver: quasar_led_screen (Screen control)" verify_driver_build "quasar_led_screen" "CONFIG_YANDEX_QUASAR_LED_SCREEN"
check_step "Driver: quasar_led_ring (IS31FL3236A ring wrapper)" verify_driver_build "quasar_led_ring" "CONFIG_YANDEX_QUASAR_LED_RING"
check_step "Driver: leds-is31fl32xx (Dual 72-ch LED driver)" verify_driver_build "leds-is31fl32xx" "CONFIG_YANDEX_LEDS_IS31FL32XX"
check_step "Driver: rotary-volume (Quadrature Gray code wheel)" verify_driver_build "rotary-volume" "CONFIG_YANDEX_ROTARY_VOLUME"
check_step "Driver: gpio_keypad (Top buttons)" verify_driver_build "gpio_keypad" "CONFIG_YANDEX_GPIO_KEYPAD"
check_step "Driver: zigbee-control (EFR32 reset/boot)" verify_driver_build "zigbee-control" "CONFIG_YANDEX_ZIGBEE_CONTROL"
check_step "Driver: ltr308als01 (Ambient light sensor)" verify_driver_build "ltr308als01" "CONFIG_YANDEX_LTR308ALS01"
check_step "Driver: quasar_pcba_ids (EEPROM PCBA revs)" verify_driver_build "quasar_pcba_ids" "CONFIG_YANDEX_QUASAR_PCBA_IDS"
check_step "Driver: snd-soc-sy6045s (Woofer/Tweeter amp)" verify_driver_build "snd-soc-sy6045s" "CONFIG_YANDEX_SND_SOC_SY6045S"
check_step "Driver: snd-soc-es8156 (Headphone DAC with DAIFMT fix)" verify_driver_build "snd-soc-es8156" "CONFIG_YANDEX_SND_SOC_ES8156"
check_step "Driver: snd-soc-es7210 (Loopback/Mic ADC)" verify_driver_build "snd-soc-es7210" "CONFIG_YANDEX_SND_SOC_ES7210"
check_step "Driver: snd-soc-tas5825m (Subwoofer amp fallback)" verify_driver_build "snd-soc-tas5825m" "CONFIG_YANDEX_SND_SOC_TAS5825M"

# ------------------------------------------------------------------------------
# Tier 4: Firmware Assets & Decoupling
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}--- Tier 4: Firmware Assets & Audio DSP Decoupling ---${NC}"

FW_ROOT="$PROJECT_ROOT/board/yandex/station2/rootfs_overlay/lib/firmware"

check_step "DSP Firmware: SY6045S woofer settings" test -s "$FW_ROOT/sy6045s-woofer-settings.txt"
check_step "DSP Firmware: SY6045S tweeters settings" test -s "$FW_ROOT/sy6045s-tweeters-settings.txt"
check_step "BT Firmware: Realtek RTL8822CS microcode" test -s "$FW_ROOT/rtlbt/rtl8822cs_fw.bin"
check_step "BT Config: Realtek RTL8822CS PSKey config" test -s "$FW_ROOT/rtlbt/rtl8822cs_config.bin"
check_step "BT Symlink: rtl_bt -> rtlbt" test -L "$FW_ROOT/rtl_bt"

# ------------------------------------------------------------------------------
# Tier 5: Userspace Scripts & Daemons Syntax Check
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}--- Tier 5: Userspace Daemons & Service Scripts ---${NC}"

USRLIB="$PROJECT_ROOT/board/yandex/station2/rootfs_overlay/usr/lib/atrios"
check_py_syntax() { python3 -m py_compile "$USRLIB/$1"; }
check_sh_syntax() { bash -n "$1"; }

check_step "Python daemon: screend (Gowin LED clock)" check_py_syntax "screend"
check_step "Python daemon: rotaryd (Volume encoder)" check_py_syntax "rotaryd"
check_step "Python daemon: light (Light & ring CLI)" check_py_syntax "light"
check_step "Python daemon: bt-agent (BlueZ pairing agent)" check_py_syntax "bt-agent"
check_step "Python daemon: bt-volume-sync (AVRCP volume sync)" check_py_syntax "bt-volume-sync"
check_step "Python tool: wifi_scan.py (nl80211 scanner)" check_py_syntax "wifi_scan.py"

INIT_DIR="$PROJECT_ROOT/board/yandex/station2/rootfs_overlay/etc/init.d"
check_step "Init script: S01modules" check_sh_syntax "$INIT_DIR/S01modules"
check_step "Init script: S03amplifiers" check_sh_syntax "$INIT_DIR/S03amplifiers"
check_step "Init script: S40network" check_sh_syntax "$INIT_DIR/S40network"
check_step "Init script: S50pipewire" check_sh_syntax "$INIT_DIR/S50pipewire"
check_step "Init script: S60atrios" check_sh_syntax "$INIT_DIR/S60atrios"
check_step "Init script: S71atrios-bt-control" check_sh_syntax "$INIT_DIR/S71atrios-bt-control"
check_step "CLI wrapper: /usr/bin/atrios" check_sh_syntax "$PROJECT_ROOT/board/yandex/station2/rootfs_overlay/usr/bin/atrios"

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
echo "=========================================================="
TOTAL=$((PASS_COUNT + FAIL_COUNT))
echo -e "Verification Complete: ${GREEN}$PASS_COUNT passed${NC}, ${RED}$FAIL_COUNT failed${NC} (Total: $TOTAL)"
if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "${GREEN}SUCCESS: 100% of all mainline validation checks passed!${NC}"
    echo "=========================================================="
    exit 0
else
    echo -e "${RED}FAILURE: $FAIL_COUNT check(s) failed.${NC}"
    echo "=========================================================="
    exit 1
fi
