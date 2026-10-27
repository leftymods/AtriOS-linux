#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Sanitize PATH to prevent Buildroot dependency errors due to Windows spaces in WSL
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PROJECT_ROOT/tools/bin"

BUILDROOT_DIR="$PROJECT_ROOT/buildroot"
BOARD_DIR="$PROJECT_ROOT/board/yandex/station2"
OVERLAY_DIR="$BOARD_DIR/rootfs_overlay"
TOOLS_DIR="$PROJECT_ROOT/tools/fw_builder"
MAKE_EXT4FS="$PROJECT_ROOT/tools/bin/make_ext4fs"
[ ! -x "$MAKE_EXT4FS" ] && MAKE_EXT4FS="$(which make_ext4fs 2>/dev/null || true)"

# Prefer the DTC shipped with the Amlogic source drop (DTC 1.4.2), then system dtc.
DTC="$PROJECT_ROOT/prebuilts/misc/linux-x86/dtc/dtc"
[ ! -x "$DTC" ] && DTC="$(which dtc 2>/dev/null || true)"

VERSION_NUM=105
BUILD_ROOTFS=1
SKIP_ROOTFS_REASON=""

usage() {
    cat <<EOF
Usage: $0 [VERSION_NUM] [options]

Options:
  -n, --version NUM    Firmware version number (default: 105)
      --no-buildroot   Skip the Buildroot rootfs build (assemble from repo assets)
  -h, --help           Show this help
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--version)
            [ -n "$2" ] || { echo "[!] --version requires a number"; exit 2; }
            VERSION_NUM="$2"; shift 2 ;;
        --no-buildroot)
            BUILD_ROOTFS=0
            SKIP_ROOTFS_REASON="--no-buildroot requested"
            shift ;;
        -h|--help)
            usage; exit 0 ;;
        ''|*[!0-9]*)
            echo "[!] Unknown argument: $1"; usage; exit 2 ;;
        *)
            VERSION_NUM="$1"; shift ;;
    esac
done

echo "=========================================================="
echo "          AtriOS Linux Master Build System                 "
echo "      Target: Yandex Station Max [Amlogic S905X3]         "
echo "=========================================================="

echo "[1/6] Verifying toolchain and build directories..."
if [ ! -x "$DTC" ]; then
    echo "[!] No usable dtc found (expected $PROJECT_ROOT/prebuilts/misc/linux-x86/dtc/dtc)"
    exit 1
fi
echo "      DTC: $DTC"

# Vendor kernel modules and firmware are extracted from the stock vendor image
# once and committed to the repo; the build must never depend on the stock image.
VENDOR_KO_COUNT=$(find "$OVERLAY_DIR/lib/modules/4.9.113" -maxdepth 1 -name '*.ko' 2>/dev/null | wc -l)
VENDOR_FW_COUNT=$(find "$OVERLAY_DIR/lib/firmware" -type f 2>/dev/null | wc -l)
if [ "$VENDOR_KO_COUNT" -lt 51 ] || [ "$VENDOR_FW_COUNT" -lt 1 ]; then
    echo "[!] Vendor payload is incomplete: $VENDOR_KO_COUNT modules, $VENDOR_FW_COUNT firmware files"
    echo "    Expected at $OVERLAY_DIR/lib/{modules/4.9.113,firmware}"
    exit 1
fi
echo "      Vendor payload: $VENDOR_KO_COUNT kernel modules, $VENDOR_FW_COUNT firmware files"

if [ "$BUILD_ROOTFS" -eq 1 ]; then
    if [ ! -d "$BUILDROOT_DIR" ]; then
        echo "[!] Buildroot source tree not found at $BUILDROOT_DIR"
        echo "[!] Fetching Buildroot 2024.02 LTS..."
        if ! git clone --depth 1 -b 2024.02.x https://github.com/buildroot/buildroot.git "$BUILDROOT_DIR"; then
            echo "[!] Buildroot clone failed. Continuing without a rootfs build."
            BUILD_ROOTFS=0
            SKIP_ROOTFS_REASON="Buildroot clone failed"
        fi
    fi
fi

# Ensure defconfig is available in Buildroot with dynamic OVERLAY_DIR
if [ "$BUILD_ROOTFS" -eq 1 ] && [ -f "$BOARD_DIR/station2_defconfig" ]; then
    mkdir -p "$BUILDROOT_DIR/configs"
    sed "s|BR2_ROOTFS_OVERLAY=.*|BR2_ROOTFS_OVERLAY=\"$OVERLAY_DIR\"|" \
        "$BOARD_DIR/station2_defconfig" > "$BUILDROOT_DIR/configs/yandex_station2_defconfig"
fi

echo "[2/6] Updating kernel modules dependency map (depmod)..."
depmod -b "$OVERLAY_DIR" 4.9.113 2>/dev/null || true

echo "[3/6] Compiling Device Tree with Dual Top Buttons..."
DTS="$TOOLS_DIR/overlay/dts/sm1_s905x3_yandex.dts"
DTB="$TOOLS_DIR/overlay/dts/sm1_s905x3_yandex.dtb"
if ! "$DTC" -I dts -O dtb -o "$DTB" "$DTS"; then
    echo "[!] Device Tree compilation failed - aborting."
    exit 1
fi
echo "      DTB: $DTB ($(stat -c %s "$DTB") bytes)"
mkdir -p "$PROJECT_ROOT/common/arch/arm64/boot/dts/amlogic"
cp "$DTB" "$PROJECT_ROOT/common/arch/arm64/boot/dts/amlogic/sm1_s905x3_yandex.dtb"

echo "[4/6] Configuring & Building Buildroot rootfs..."
if [ "$BUILD_ROOTFS" -eq 1 ]; then
    cd "$BUILDROOT_DIR"
    echo "Applying AtriOS Station 2 configuration..."
    make yandex_station2_defconfig

    make -j"$(nproc)"
    cd "$PROJECT_ROOT"
else
    echo "[!] Skipping Buildroot build ($SKIP_ROOTFS_REASON)."
fi

echo "[5/6] Generating ext4 system.PARTITION from Buildroot rootfs..."
ROOTFS_TAR="$BUILDROOT_DIR/output/images/rootfs.tar"
mkdir -p "$TOOLS_DIR/staging"
if [ -f "$ROOTFS_TAR" ]; then
    echo "Rootfs archive found: $ROOTFS_TAR"
    mkdir -p "$TOOLS_DIR/overlay/system"
    rm -rf "$TOOLS_DIR/overlay/system"/* 2>/dev/null || true
    tar -xf "$ROOTFS_TAR" -C "$TOOLS_DIR/overlay/system"
    ln -sf sbin/init "$TOOLS_DIR/overlay/system/init"
    chmod 755 "$TOOLS_DIR/overlay/system/lib/ld-"* 2>/dev/null || true
    chmod 755 "$TOOLS_DIR/overlay/system/bin/busybox" 2>/dev/null || true

    # The rootfs must carry the vendor kernel modules and their firmware.
    ROOTFS_KO_COUNT=$(find "$TOOLS_DIR/overlay/system/lib/modules/4.9.113" -maxdepth 1 -name '*.ko' 2>/dev/null | wc -l)
    ROOTFS_FW_COUNT=$(find "$TOOLS_DIR/overlay/system/lib/firmware" -type f 2>/dev/null | wc -l)
    if [ "$ROOTFS_KO_COUNT" -lt 51 ] || [ "$ROOTFS_FW_COUNT" -lt 1 ]; then
        echo "[!] Rootfs is missing the vendor payload: $ROOTFS_KO_COUNT modules, $ROOTFS_FW_COUNT firmware files"
        echo "    Check BR2_ROOTFS_OVERLAY=$OVERLAY_DIR"
        exit 1
    fi
    echo "      Rootfs vendor payload: $ROOTFS_KO_COUNT kernel modules, $ROOTFS_FW_COUNT firmware files"

    if [ -x "$MAKE_EXT4FS" ]; then
        echo "Building raw ext4 system.PARTITION (1.44 GB)..."
        "$MAKE_EXT4FS" -l 1551892480 "$TOOLS_DIR/staging/system.PARTITION" "$TOOLS_DIR/overlay/system"
        touch "$TOOLS_DIR/.system_from_rootfs"
    else
        echo "[!] make_ext4fs not found - fw_builder will generate the system image."
    fi
else
    echo "[!] No rootfs.tar found. fw_builder will create a placeholder system.PARTITION."
    echo "[!] The resulting image will pack and verify but will NOT boot into AtriOS."
fi

# Keep the boot cmdline's DT id in sync with the ported DTS amlogic-dt-id.
CMDLINE="$TOOLS_DIR/overlay/boot/cmdline.txt"
if [ -f "$CMDLINE" ] && grep -q 'androidboot.hardware.amlogic_dt_id=' "$CMDLINE"; then
    sed -i 's|androidboot.hardware.amlogic_dt_id=[^ ]*|androidboot.hardware.amlogic_dt_id=yandex_station_2g|' "$CMDLINE"
fi

echo "[6/6] Packaging and signing AtriOS firmware image..."
cd "$TOOLS_DIR"
rm -f .fw_builder_cache.json

BUILD_ARGS=(build --version-num "$VERSION_NUM")
echo "      Source: repository assets (bootloader, U-Boot, logo, DTS, kernel, vendor modules) - no stock image."

python3 "$TOOLS_DIR/fw_builder.py" "${BUILD_ARGS[@]}"
echo "=========================================================="
echo "SUCCESS: AtriOS Linux Firmware build complete!"
echo "Image location: $TOOLS_DIR/yandex_s905x3_custom_v${VERSION_NUM}.img"
echo "=========================================================="
