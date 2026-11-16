#!/bin/bash
# ==============================================================================
# AtriOS Build System - Mainline U-Boot & FIP Builder for Station Max
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

UBOOT_SRC="${UBOOT_SRC:-$PROJECT_ROOT/cache/uboot}"
FIP_TOOL_DIR="${FIP_TOOL_DIR:-$PROJECT_ROOT/cache/fip}"
BOARD_DIR="$PROJECT_ROOT/board/yandex/station2/mainline"
OUTPUT_DIR="$PROJECT_ROOT/device/yandex/station_max/upgrade"

echo "=========================================================="
echo "    Mainline U-Boot Build & FIP Signer for Station Max    "
echo "=========================================================="

CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
export ARCH=arm
export CROSS_COMPILE

# 1. Fetch / verify Mainline U-Boot tree from leftymods
if [ ! -d "$UBOOT_SRC" ]; then
    if [ -d "$PROJECT_ROOT/u-boot-mainline" ]; then
        mkdir -p "$PROJECT_ROOT/cache"
        ln -sfn "$PROJECT_ROOT/u-boot-mainline" "$UBOOT_SRC"
    else
        echo "[1/4] Cloning leftymods U-Boot..."
        mkdir -p "$PROJECT_ROOT/cache"
        git clone --depth 1 -b master https://github.com/leftymods/u-boot.git "$UBOOT_SRC"
    fi
fi

# 2. Inject Station Max DTS (наш DTS)
echo "[2/4] Injecting Station Max DTS into U-Boot..."
mkdir -p "$UBOOT_SRC/dts/upstream/src/arm64/amlogic" "$UBOOT_SRC/arch/arm/dts"
cp "$BOARD_DIR/dts/meson-sm1-yandex-station-max.dts" "$UBOOT_SRC/dts/upstream/src/arm64/amlogic/meson-sm1-atristation.dts"
cp "$BOARD_DIR/aml_burn/meson-sm1-atristation-u-boot.dtsi" "$UBOOT_SRC/arch/arm/dts/meson-sm1-atristation-u-boot.dtsi"

# Install the sources actually audited and build the repository-owned config.
bash "$BOARD_DIR/aml_burn/install.sh" "$UBOOT_SRC"
cp "$BOARD_DIR/uboot_station_max_defconfig" "$UBOOT_SRC/configs/atrios_station_max_defconfig"

# 3. Compile Mainline U-Boot
echo "[3/4] Compiling Mainline U-Boot with atrios_station_max_defconfig..."
(
    cd "$UBOOT_SRC"
    make atrios_station_max_defconfig
    for option in CONFIG_CMD_AML_BURN CONFIG_USB_FUNCTION_AML_BURN CONFIG_USB_DWC3_GADGET; do
        grep -qx "${option}=y" .config || { echo "ERROR: missing $option" >&2; exit 1; }
    done
    make -j"$(nproc 2>/dev/null || echo 4)"
)

MAINLINE_UBOOT_BIN="$UBOOT_SRC/u-boot.bin"
if [ ! -f "$MAINLINE_UBOOT_BIN" ]; then
    echo "ERROR: Mainline U-Boot compilation failed to generate u-boot.bin"
    exit 1
fi
echo "      Generated Mainline U-Boot: $MAINLINE_UBOOT_BIN ($(stat -c %s "$MAINLINE_UBOOT_BIN") bytes)"

# 4. Sign and package with LibreELEC amlogic-boot-fip (sei610 profile)
echo "[4/4] Signing with SEI610 FIP..."
if [ ! -d "$FIP_TOOL_DIR/sei610" ]; then
    if [ -e "$FIP_TOOL_DIR" ]; then
        echo "ERROR: incomplete FIP tool tree: $FIP_TOOL_DIR" >&2
        exit 1
    else
        echo "Cloning amlogic-boot-fip..."
        git clone --depth 1 https://github.com/LibreELEC/amlogic-boot-fip.git "$FIP_TOOL_DIR"
    fi
fi

mkdir -p "$OUTPUT_DIR"
(
    cd "$FIP_TOOL_DIR"
    ./build-fip.sh sei610 "$MAINLINE_UBOOT_BIN" "$OUTPUT_DIR"
)

# Ensure bootloader.img matches u-boot.bin
cp "$OUTPUT_DIR/u-boot.bin" "$OUTPUT_DIR/bootloader.img"

echo "=========================================================="
echo "SUCCESS: Mainline U-Boot signed & packaged!"
echo "Outputs: $OUTPUT_DIR/bootloader.img"
echo "         $OUTPUT_DIR/u-boot.bin.sd.bin"
echo "         $OUTPUT_DIR/u-boot.bin.usb.bl2"
echo "         $OUTPUT_DIR/u-boot.bin.usb.tpl"
echo "=========================================================="
