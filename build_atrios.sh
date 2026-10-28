#!/bin/bash
# AtriOS Linux firmware build - fully scripted, one command:
#
#   ./build_atrios.sh              # build rootfs + image, next version number
#   ./build_atrios.sh 130          # explicit version
#   ./build_atrios.sh 130 --no-buildroot   # reuse the last rootfs.tar
#
# Everything the build needs is either tracked in this repository
# (board/, device/, tools/bin, tools/fw_builder, prebuilts/.../dtc) or
# fetched/compiled here (Buildroot itself).  No manual staging of files.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Keep a predictable PATH; Buildroot rejects some host setups otherwise.
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PROJECT_ROOT/tools/bin"
export LC_ALL=C

BUILDROOT_DIR="$PROJECT_ROOT/buildroot"
BOARD_DIR="$PROJECT_ROOT/board/yandex/station2"
OVERLAY_DIR="$BOARD_DIR/rootfs_overlay"
TOOLS_DIR="$PROJECT_ROOT/tools/fw_builder"
STAGING_DIR="$TOOLS_DIR/staging"
SYSTEM_TREE="$TOOLS_DIR/overlay/system"
ROOTFS_TAR="$BUILDROOT_DIR/output/images/rootfs.tar"
MAKE_EXT4FS="$PROJECT_ROOT/tools/bin/make_ext4fs"
AMPACK="$PROJECT_ROOT/tools/bin/ampack"
DTC="$PROJECT_ROOT/prebuilts/misc/linux-x86/dtc/dtc"
SYSTEM_PARTITION_SIZE=1551892480

VERSION_NUM=""
BUILD_ROOTFS=1
JOBS=""

usage() {
    cat <<EOF
Usage: $0 [VERSION_NUM] [options]

Options:
  -n, --version NUM    Firmware version number (default: contents of ./VERSION)
  -j, --jobs N         Parallel build jobs (default: min(nproc, 6))
      --no-buildroot   Reuse the existing Buildroot rootfs.tar instead of rebuilding
  -h, --help           Show this help

Prerequisites (Debian/Ubuntu):
  sudo apt-get install -y gcc-13 g++-13 make bc bison flex \
      libssl-dev libncurses-dev libelf-dev rsync cpio unzip \
      device-tree-compiler python3 kmod
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--version)
            [ -n "${2:-}" ] || { echo "[!] --version requires a number"; exit 2; }
            VERSION_NUM="$2"; shift 2 ;;
        -j|--jobs)
            [ -n "${2:-}" ] || { echo "[!] --jobs requires a number"; exit 2; }
            JOBS="$2"; shift 2 ;;
        --no-buildroot)
            BUILD_ROOTFS=0; shift ;;
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

# ---------------------------------------------------------------------------
# [0/6] Host compiler
# ---------------------------------------------------------------------------
# Buildroot 2024.02 predates GCC 14/15 and fails on them.  Pick the newest
# host compiler that is known to work instead of relying on the caller.
host_gcc_major() { "$1" -dumpversion 2>/dev/null | cut -d. -f1; }

if [ -z "${HOSTCC:-}" ]; then
    for c in gcc-13 gcc-12 gcc; do
        command -v "$c" >/dev/null 2>&1 || continue
        major="$(host_gcc_major "$c")"
        [ -n "$major" ] || continue
        if [ "$major" -le 13 ]; then
            HOSTCC="$c"
            break
        fi
    done
fi

if [ -z "${HOSTCC:-}" ] || [ "$(host_gcc_major "$HOSTCC")" -gt 13 ]; then
    echo "[!] No host GCC <= 13 found (Buildroot 2024.02 does not build with 14+)."
    echo "    Install it:  sudo apt-get install -y gcc-13 g++-13"
    exit 1
fi
ALIASED_CXX="${HOSTCC/gcc/g++}"
if [ -z "${HOSTCXX:-}" ]; then
    if command -v "$ALIASED_CXX" >/dev/null 2>&1; then
        HOSTCXX="$ALIASED_CXX"
    else
        HOSTCXX="$HOSTCC"
    fi
fi
export HOSTCC HOSTCXX

# A full Buildroot build peaks at roughly 1 GB per compiler job; capping the
# default keeps 8 GB machines from swapping while still using them fully.
if [ -z "$JOBS" ]; then
    JOBS="$(nproc)"
    [ "$JOBS" -gt 6 ] && JOBS=6
fi
echo "[0/6] Host compiler: $HOSTCC ($("$HOSTCC" -dumpversion)) / $HOSTCXX, jobs: $JOBS"

# ---------------------------------------------------------------------------
# [1/6] Tools and inputs
# ---------------------------------------------------------------------------
for tool in "$DTC" "$MAKE_EXT4FS" "$AMPACK" python3 rsync tar; do
    if [ ! -x "$tool" ] && ! command -v "$tool" >/dev/null 2>&1; then
        echo "[!] Required build tool not found: $tool"
        exit 1
    fi
done
echo "[1/6] Build tools present (dtc, make_ext4fs, ampack, python3)"

VENDOR_KO_COUNT=$(find "$OVERLAY_DIR/lib/modules/4.9.113" -maxdepth 1 -name '*.ko' 2>/dev/null | wc -l)
VENDOR_FW_COUNT=$(find "$OVERLAY_DIR/lib/firmware" -type f 2>/dev/null | wc -l)
if [ "$VENDOR_KO_COUNT" -lt 51 ] || [ "$VENDOR_FW_COUNT" -lt 1 ]; then
    echo "[!] Vendor payload is incomplete: $VENDOR_KO_COUNT modules, $VENDOR_FW_COUNT firmware files"
    echo "    Expected at $OVERLAY_DIR/lib/{modules/4.9.113,firmware}"
    exit 1
fi
echo "      Vendor payload: $VENDOR_KO_COUNT kernel modules, $VENDOR_FW_COUNT firmware files"

# The bootloader partition is shipped as a repository asset; refuse to build
# an image whose bootloader asset is missing rather than silently packing a
# placeholder.
for asset in bootloader.img u-boot.bin.sd.bin platform.conf aml_sdc_burn.ini logo.PARTITION; do
    if [ ! -s "$PROJECT_ROOT/device/yandex/station_max/upgrade/$asset" ]; then
        echo "[!] Missing bootloader asset: device/yandex/station_max/upgrade/$asset"
        exit 1
    fi
done

# Version: the tracked VERSION file is the release number this tree builds.
# Bump it (and commit) when cutting a new image; -n overrides for a one-off.
if [ -z "$VERSION_NUM" ] && [ -s "$PROJECT_ROOT/VERSION" ]; then
    VERSION_NUM="$(tr -dc '0-9' < "$PROJECT_ROOT/VERSION")"
fi
if [ -z "$VERSION_NUM" ]; then
    last="$(ls "$TOOLS_DIR"/yandex_s905x3_custom_v*.img 2>/dev/null \
            | sed 's/.*_v//; s/\.img$//' | sort -n | tail -1)"
    VERSION_NUM=$(( ${last:-0} + 1 ))
fi
echo "      Version: v$VERSION_NUM"

# ---------------------------------------------------------------------------
# [2/6] Buildroot rootfs
# ---------------------------------------------------------------------------
if [ "$BUILD_ROOTFS" -eq 1 ]; then
    if [ ! -d "$BUILDROOT_DIR" ]; then
        echo "[2/6] Buildroot source tree not found; cloning 2024.02 LTS..."
        git clone --depth 1 -b 2024.02.x https://github.com/buildroot/buildroot.git "$BUILDROOT_DIR"
    fi

    echo "[2/6] Configuring Buildroot (overlay: $OVERLAY_DIR)..."
    mkdir -p "$BUILDROOT_DIR/configs"
    sed "s|BR2_ROOTFS_OVERLAY=.*|BR2_ROOTFS_OVERLAY=\"$OVERLAY_DIR\"|" \
        "$BOARD_DIR/station2_defconfig" > "$BUILDROOT_DIR/configs/yandex_station2_defconfig"

    depmod -b "$OVERLAY_DIR" 4.9.113 2>/dev/null || true

    (
        cd "$BUILDROOT_DIR"
        make yandex_station2_defconfig
        make -j"$JOBS" HOSTCC="$HOSTCC" HOSTCXX="$HOSTCXX"
    )

    if [ ! -s "$ROOTFS_TAR" ]; then
        echo "[!] Buildroot finished without $ROOTFS_TAR"
        exit 1
    fi
    echo "      rootfs.tar: $(stat -c %s "$ROOTFS_TAR") bytes"
else
    echo "[2/6] Skipping Buildroot build (--no-buildroot)"
    if [ ! -s "$ROOTFS_TAR" ]; then
        echo "[!] --no-buildroot requires an existing $ROOTFS_TAR"
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# [3/6] Device tree
# ---------------------------------------------------------------------------
echo "[3/6] Compiling device tree..."
DTS="$TOOLS_DIR/overlay/dts/sm1_s905x3_yandex.dts"
DTB="$TOOLS_DIR/overlay/dts/sm1_s905x3_yandex.dtb"
"$DTC" -I dts -O dtb -o "$DTB" "$DTS" 2>/dev/null
echo "      DTB: $DTB ($(stat -c %s "$DTB") bytes)"

# ---------------------------------------------------------------------------
# [4/6] system.PARTITION from rootfs.tar + rootfs_overlay
# ---------------------------------------------------------------------------
echo "[4/6] Assembling system.PARTITION..."
rm -rf "$SYSTEM_TREE"
mkdir -p "$SYSTEM_TREE"
tar -xf "$ROOTFS_TAR" -C "$SYSTEM_TREE"

# Re-apply the repository overlay on top of the tarball so that a refreshed
# --no-buildroot assembly still gets the current scripts.
rsync -a "$OVERLAY_DIR"/ "$SYSTEM_TREE"/

ln -sf sbin/init "$SYSTEM_TREE/init"
chmod 755 "$SYSTEM_TREE/lib/ld-"* 2>/dev/null || true
chmod 755 "$SYSTEM_TREE/bin/busybox" 2>/dev/null || true

ROOTFS_KO_COUNT=$(find "$SYSTEM_TREE/lib/modules/4.9.113" -maxdepth 1 -name '*.ko' 2>/dev/null | wc -l)
ROOTFS_FW_COUNT=$(find "$SYSTEM_TREE/lib/firmware" -type f 2>/dev/null | wc -l)
if [ "$ROOTFS_KO_COUNT" -lt 51 ] || [ "$ROOTFS_FW_COUNT" -lt 1 ]; then
    echo "[!] Rootfs is missing the vendor payload: $ROOTFS_KO_COUNT modules, $ROOTFS_FW_COUNT firmware files"
    exit 1
fi

mkdir -p "$STAGING_DIR"
"$MAKE_EXT4FS" -l "$SYSTEM_PARTITION_SIZE" -a system "$STAGING_DIR/system.PARTITION" "$SYSTEM_TREE"
touch "$TOOLS_DIR/.system_from_rootfs"
echo "      system.PARTITION: $(stat -c %s "$STAGING_DIR/system.PARTITION") bytes"
echo "      Rootfs vendor payload: $ROOTFS_KO_COUNT kernel modules, $ROOTFS_FW_COUNT firmware files"

# ---------------------------------------------------------------------------
# [5/6] Package and verify
# ---------------------------------------------------------------------------
echo "[5/6] Packaging firmware image..."
IMG="$TOOLS_DIR/yandex_s905x3_custom_v${VERSION_NUM}.img"
(
    cd "$TOOLS_DIR"
    rm -f .fw_builder_cache.json
    python3 fw_builder.py build --version-num "$VERSION_NUM"
)

if [ ! -s "$IMG" ]; then
    echo "[!] Firmware image was not produced: $IMG"
    exit 1
fi

echo "[6/6] Verifying image..."
"$AMPACK" verify "$IMG" >/dev/null
sha256sum "$IMG" | tee "$IMG.sha256"
ls -lh "$IMG"

echo "=========================================================="
echo "SUCCESS: AtriOS Linux v$VERSION_NUM"
echo "Image:  $IMG"
echo "SHA256: $(cut -d' ' -f1 "$IMG.sha256")"
echo "=========================================================="
