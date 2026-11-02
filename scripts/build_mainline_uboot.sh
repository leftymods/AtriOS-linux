#!/bin/bash
#
# Build script for Mainline U-Boot on Yandex Station Max (SM1 / S905X3)
# Produces signed bootloader.img and u-boot.bin.sd.bin using Amlogic FIP encryption tools.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
UBOOT_SRC="${UBOOT_SRC:-$PROJECT_ROOT/cache/uboot}"
if [ ! -d "$UBOOT_SRC" ]; then
    if [ -d "$PROJECT_ROOT/u-boot-mainline" ]; then
        mkdir -p "$PROJECT_ROOT/cache"
        ln -sfn "$PROJECT_ROOT/u-boot-mainline" "$UBOOT_SRC"
    fi
fi
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
mkdir -p "$UBOOT_SRC/arch/arm/dts" "$UBOOT_SRC/dts/upstream/src/arm64"
cp "$BOARD_DIR/dts/meson-sm1-yandex-station-max.dts" "$UBOOT_SRC/arch/arm/dts/"
cp "$BOARD_DIR/dts/meson-sm1-yandex-station-max-u-boot.dtsi" "$UBOOT_SRC/arch/arm/dts/"
cp "$BOARD_DIR/dts/meson-sm1-yandex-station-max.dts" "$UBOOT_SRC/dts/upstream/src/arm64/"
cp "$BOARD_DIR/dts/meson-sm1-yandex-station-max-u-boot.dtsi" "$UBOOT_SRC/dts/upstream/src/arm64/"
cp "$BOARD_DIR/uboot_station_max_defconfig" "$UBOOT_SRC/configs/station_max_defconfig"
bash "$BOARD_DIR/aml_burn/install.sh" "$UBOOT_SRC"

# Ensure mainline Amlogic init SP address and disable misc_init_r if board driver not present
if ! grep -q "CONFIG_HAS_CUSTOM_SYS_INIT_SP_ADDR" "$UBOOT_SRC/configs/station_max_defconfig"; then
    echo "CONFIG_HAS_CUSTOM_SYS_INIT_SP_ADDR=y" >> "$UBOOT_SRC/configs/station_max_defconfig"
    echo "CONFIG_CUSTOM_SYS_INIT_SP_ADDR=0x20000000" >> "$UBOOT_SRC/configs/station_max_defconfig"
fi
sed -i 's/CONFIG_MISC_INIT_R=y/# CONFIG_MISC_INIT_R is not set/' "$UBOOT_SRC/configs/station_max_defconfig"

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

FIP_BLOBS="$FIP_DIR/g12a"
AML_ENCRYPT="$FIP_BLOBS/aml_encrypt_g12a"

# Ensure clean upstream blobs exist
for req_blob in bl2.bin acs.bin bl30.bin bl301.bin bl31.img aml_ddr.fw piei.fw lpddr4_1d.fw lpddr4_2d.fw diag_lpddr4.fw; do
    if [ ! -f "$FIP_BLOBS/$req_blob" ]; then
        echo "ERROR: Missing required upstream blob: $FIP_BLOBS/$req_blob"
        exit 1
    fi
done

cp "$FIP_BLOBS/bl2.bin" "$TMP_FIP/bl2.bin"
cp "$FIP_BLOBS/acs.bin" "$TMP_FIP/acs.bin"
cp "$FIP_BLOBS/bl30.bin" "$TMP_FIP/bl30.bin"
cp "$FIP_BLOBS/bl301.bin" "$TMP_FIP/bl301.bin"
cp "$FIP_BLOBS/bl31.img" "$TMP_FIP/bl31.img"
cp "$MAINLINE_UBOOT_BIN" "$TMP_FIP/bl33.bin"

# Run Amlogic encrypt stages
(
    cd "$PROJECT_ROOT/bootloader/uboot-repo"
    export FIP_FOLDER="fip/"
    export CUR_SOC="g12a"
    export BUILD_PATH="fip/_tmp"
    export BL3X_SUFFIX="img"

    # Stitch bl2 + acs (61440 bytes) and bl30 + bl301 (54272 bytes)
    python3 -c "
def stitch(b1, b2, out_f, l1, l2):
    d1 = open(b1, 'rb').read()
    d2 = open(b2, 'rb').read()
    assert len(d1) <= l1, f'{b1} size {len(d1)} > {l1}'
    assert len(d2) <= l2, f'{b2} size {len(d2)} > {l2}'
    data = d1.ljust(l1, b'\x00') + d2.ljust(l2, b'\x00')
    assert len(data) == (l1 + l2), f'Output size {len(data)} != {l1 + l2}'
    open(out_f, 'wb').write(data)

stitch('fip/_tmp/bl30.bin', 'fip/_tmp/bl301.bin', 'fip/_tmp/bl30_new.bin', 40960, 13312)
stitch('fip/_tmp/bl2.bin',  'fip/_tmp/acs.bin',   'fip/_tmp/bl2_new.bin',  57344, 4096)

# Verify acs__ signature in bl2_new.bin
bl2_new = open('fip/_tmp/bl2_new.bin', 'rb').read()
pos = bl2_new.find(b'acs__')
assert pos == 57344 + 432, f'acs__ signature invalid: {pos}'
print(f'      [+] Verified bl2_new.bin: acs__ signature at offset {pos} (0x{pos:x})')
"

    # Sign BL30, BL31, BL33 and BL2
    $AML_ENCRYPT --bl30sig --input fip/_tmp/bl30_new.bin --output fip/_tmp/bl30_new.bin.g12a.enc --level v3
    $AML_ENCRYPT --bl3sig  --input fip/_tmp/bl30_new.bin.g12a.enc --output fip/_tmp/bl30_new.bin.enc --level v3 --type bl30
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

# Generate split USB boot binaries for Amlogic USB boot protocol
# u-boot.bin.usb.bl2: first 65,536 bytes (BL2 stage loaded by BootROM into SRAM 0xfffa0000)
head -c 65536 "$TMP_FIP/u-boot.bin" > "$OUTPUT_DIR/u-boot.bin.usb.bl2"

# u-boot.bin.usb.tpl: from byte 65,537 to EOF (FIP container with DDR FW, BL30, BL31, BL33)
tail -c +65537 "$TMP_FIP/u-boot.bin" > "$OUTPUT_DIR/u-boot.bin.usb.tpl"

# Generate SD boot binary with 512-byte header required for SD card MBR offset alignment
dd if=/dev/urandom of="$OUTPUT_DIR/u-boot.bin.sd.bin" bs=512 count=1 status=none
cat "$TMP_FIP/u-boot.bin" >> "$OUTPUT_DIR/u-boot.bin.sd.bin"

# Integrity sanity checks
test "$(stat -c %s "$OUTPUT_DIR/u-boot.bin.usb.bl2")" -eq 65536 || { echo "ERROR: Invalid usb.bl2 size"; exit 1; }
test "$(stat -c %s "$OUTPUT_DIR/u-boot.bin.sd.bin")" -eq "$(( $(stat -c %s "$OUTPUT_DIR/bootloader.img") + 512 ))" || { echo "ERROR: Invalid sd.bin size"; exit 1; }

echo "=========================================================="
echo "SUCCESS: Mainline U-Boot signed & packaged!"
echo "Outputs: $OUTPUT_DIR/bootloader.img"
echo "         $OUTPUT_DIR/u-boot.bin.usb.bl2"
echo "         $OUTPUT_DIR/u-boot.bin.usb.tpl"
echo "         $OUTPUT_DIR/u-boot.bin.sd.bin"
echo "=========================================================="
