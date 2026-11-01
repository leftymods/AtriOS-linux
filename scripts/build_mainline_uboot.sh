#!/bin/bash
#
# Build script for Mainline U-Boot on Yandex Station Max (SM1 / S905X3)
# Produces signed bootloader.img and u-boot.bin.sd.bin using Amlogic FIP encryption tools.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
UBOOT_SRC="${UBOOT_SRC:-$PROJECT_ROOT/u-boot-mainline}"
FIP_DIR="$PROJECT_ROOT/bootloader/uboot-repo/fip"
BOARD_DIR="$PROJECT_ROOT/board/yandex/station2/mainline"
OUTPUT_DIR="$PROJECT_ROOT/device/yandex/station_max/upgrade"

echo "=========================================================="
echo "    Mainline U-Boot Build & FIP Signer for Station Max    "
echo "=========================================================="

CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
export ARCH=arm
export CROSS_COMPILE

# 1. Fetch Mainline U-Boot if not present
if [ ! -d "$UBOOT_SRC" ]; then
    echo "[1/4] Cloning Mainline U-Boot (v2025.01)..."
    git clone --depth 1 -b v2025.01 https://github.com/u-boot/u-boot.git "$UBOOT_SRC"
fi

# 2. Copy Defconfig & DTS
echo "[2/4] Injecting Station Max DTS & Defconfig into U-Boot..."
mkdir -p "$UBOOT_SRC/arch/arm/dts"
cp "$BOARD_DIR/dts/meson-sm1-yandex-station-max.dts" "$UBOOT_SRC/arch/arm/dts/"
cp "$BOARD_DIR/dts/meson-sm1-yandex-station-max-u-boot.dtsi" "$UBOOT_SRC/arch/arm/dts/"
cp "$BOARD_DIR/uboot_station_max_defconfig" "$UBOOT_SRC/configs/station_max_defconfig"

# 3. Build Mainline U-Boot (bl33.bin)
echo "[3/4] Compiling Mainline U-Boot..."
(
    cd "$UBOOT_SRC"
    make station_max_defconfig
    make -j"$(nproc)"
)

MAINLINE_UBOOT_BIN="$UBOOT_SRC/u-boot.bin"
if [ ! -f "$MAINLINE_UBOOT_BIN" ]; then
    echo "ERROR: Mainline U-Boot build failed to generate u-boot.bin"
    exit 1
fi
echo "      Generated Mainline U-Boot: $MAINLINE_UBOOT_BIN ($(stat -c %s "$MAINLINE_UBOOT_BIN") bytes)"

# 4. FIP Packaging & Signing with SM1 blobs
echo "[4/4] Signing with Amlogic SM1 FIP binaries..."
TMP_FIP="$PROJECT_ROOT/bootloader/uboot-repo/fip/_tmp"
rm -rf "$TMP_FIP"
mkdir -p "$TMP_FIP"

BL2_BIN="$PROJECT_ROOT/bootloader/uboot-repo/bl2/bin/g12a/bl2.bin"
BL30_BIN="$PROJECT_ROOT/bootloader/uboot-repo/bl30/bin/g12a/bl30.bin"
BL31_IMG="$PROJECT_ROOT/bootloader/uboot-repo/bl31_1.3/bin/g12a/bl31.img"
AML_ENCRYPT="$FIP_DIR/g12a/aml_encrypt_g12a"

cp "$BL2_BIN" "$TMP_FIP/bl2.bin"
cp "$BL30_BIN" "$TMP_FIP/bl30.bin"
cp "$BL31_IMG" "$TMP_FIP/bl31.img"
cp "$MAINLINE_UBOOT_BIN" "$TMP_FIP/bl33.bin"

# Run Amlogic encrypt stages
(
    cd "$PROJECT_ROOT/bootloader/uboot-repo"
    export FIP_FOLDER="fip/"
    export CUR_SOC="g12a"
    export BUILD_PATH="fip/_tmp"
    export BL3X_SUFFIX="img"

    # Padding and fixing blx binaries
    python3 -c "
import os
def fix_pad(in_f, out_f, limit):
    d = open(in_f, 'rb').read()
    assert len(d) <= limit, f'{in_f} size {len(d)} > {limit}'
    open(out_f, 'wb').write(d + b'\x00' * (limit - len(d)))

fix_pad('fip/_tmp/bl30.bin', 'fip/_tmp/bl30_new.bin', 40960 + 13312)
fix_pad('fip/_tmp/bl2.bin', 'fip/_tmp/bl2_new.bin', 57344 + 4096)
"
    # Sign BL30, BL31, BL33 and BL2
    $AML_ENCRYPT --bl30sig --input fip/_tmp/bl30_new.bin --output fip/_tmp/bl30_new.bin.g12.enc --level v3
    $AML_ENCRYPT --bl3sig  --input fip/_tmp/bl30_new.bin.g12.enc --output fip/_tmp/bl30_new.bin.enc --level v3 --type bl30
    $AML_ENCRYPT --bl3sig  --input fip/_tmp/bl31.img --output fip/_tmp/bl31.img.enc --level v3 --type bl31
    $AML_ENCRYPT --bl3sig  --input fip/_tmp/bl33.bin --output fip/_tmp/bl33.bin.enc --level v3 --type bl33
    $AML_ENCRYPT --bl2sig  --input fip/_tmp/bl2_new.bin --output fip/_tmp/bl2.n.bin.sig

    # Assemble final signed bootloader
    $AML_ENCRYPT --bootmk --output fip/_tmp/u-boot.bin \
        --bl2 fip/_tmp/bl2.n.bin.sig \
        --bl30 fip/_tmp/bl30_new.bin.enc \
        --bl31 fip/_tmp/bl31.img.enc \
        --bl33 fip/_tmp/bl33.bin.enc \
        --level v3 \
        --ddrfw1 fip/g12a/ddr4_1d.fw --ddrfw2 fip/g12a/ddr4_2d.fw \
        --ddrfw3 fip/g12a/ddr3_1d.fw --ddrfw4 fip/g12a/piei.fw \
        --ddrfw5 fip/g12a/lpddr4_1d.fw --ddrfw6 fip/g12a/lpddr4_2d.fw \
        --ddrfw7 fip/g12a/diag_lpddr4.fw --ddrfw8 fip/g12a/aml_ddr.fw \
        --ddrfw9 fip/g12a/lpddr3_1d.fw
)

mkdir -p "$OUTPUT_DIR"
cp "$TMP_FIP/u-boot.bin" "$OUTPUT_DIR/bootloader.img"
cp "$TMP_FIP/u-boot.bin" "$OUTPUT_DIR/u-boot.bin.sd.bin"

echo "=========================================================="
echo "SUCCESS: Mainline U-Boot signed & packaged!"
echo "Outputs: $OUTPUT_DIR/bootloader.img"
echo "         $OUTPUT_DIR/u-boot.bin.sd.bin"
echo "=========================================================="
