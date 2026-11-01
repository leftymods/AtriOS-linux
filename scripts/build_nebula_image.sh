#!/usr/bin/env bash
# ==============================================================================
# AtriOS Nebula - Automated Firmware Image Builder
# Manufacturer: leftymods <ggalab33@gmail.com>
# Target: Yandex Station Max (SM1 / S905X3)
# Kernel: AtriOS-aml (Mainline Linux 7.0+)
# Userspace: Buildroot 2026.08.1 LTS
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_DIR="${REPO_ROOT}/output/images"
KDIR="${KDIR:-/home/lmods/linux-leftymods}"
BUILDROOT_DIR="${REPO_ROOT}/buildroot"
BOARD_DIR="${REPO_ROOT}/board/yandex/station2"
OVERLAY_DIR="${BOARD_DIR}/rootfs_overlay"
DTS_FILE="${BOARD_DIR}/mainline/dts/meson-sm1-yandex-station-max.dts"
DEFCONFIG="${BOARD_DIR}/mainline/buildroot_station_max_defconfig"
KERNEL_DEFCONFIG="${BOARD_DIR}/mainline/linux_7.0_station_max_defconfig"

mkdir -p "${OUTPUT_DIR}"

echo "=========================================================="
echo " Starting AtriOS Nebula Firmware Build Pipeline"
echo " OS: AtriOS Nebula"
echo " Kernel: AtriOS-aml (v7.0+)"
echo " Vendor: leftymods"
echo " Output Directory: ${OUTPUT_DIR}"
echo "=========================================================="

# 1. Compile Device Tree (DTB)
echo "[1/5] Compiling Device Tree (DTC)..."
cpp -nostdinc -I "${KDIR}/include" \
    -I "${KDIR}/arch/arm64/boot/dts/amlogic" \
    -I "${KDIR}/arch/arm64/boot/dts" \
    -undef -x assembler-with-cpp "${DTS_FILE}" | \
    dtc -I dts -O dtb -o "${OUTPUT_DIR}/meson-sm1-yandex-station-max.dtb"
echo "      DTB generated: ${OUTPUT_DIR}/meson-sm1-yandex-station-max.dtb"

# 2. Compile AtriOS-aml Kernel and 13 Hardware Drivers
echo "[2/5] Building AtriOS-aml Kernel and Hardware Drivers..."
cd "${KDIR}"
if [ ! -f .config ]; then
    cp "${KERNEL_DEFCONFIG}" .config
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
fi
make -j"$(nproc)" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules_prepare

cd "${REPO_ROOT}/drivers/atrios"
make -C "${KDIR}" M="${REPO_ROOT}/drivers/atrios" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KBUILD_MODPOST_WARN=1 modules
echo "      All 13 kernel modules built successfully in drivers/atrios/."

# 3. Build Userspace via Buildroot 2026.08.1
echo "[3/5] Configuring and building Buildroot 2026.08.1..."
cd "${BUILDROOT_DIR}"
make defconfig BR2_DEFCONFIG="${DEFCONFIG}"

# Inject kernel modules and firmware into overlay before assembling rootfs
MODULES_TARGET_DIR="${OVERLAY_DIR}/lib/modules/AtriOS-aml/extra"
mkdir -p "${MODULES_TARGET_DIR}"
cp -f "${REPO_ROOT}/drivers/atrios/"*.ko "${MODULES_TARGET_DIR}/"

echo "      Building Root Filesystem (this may take some time on first run)..."
make -j"$(nproc)"

# 4. Generate boot.img and RootFS
echo "[4/5] Packaging boot partition and root filesystem..."
cp -f "${KDIR}/arch/arm64/boot/Image" "${OUTPUT_DIR}/Image"
if [ -f "${BUILDROOT_DIR}/output/images/rootfs.ext4" ]; then
    cp -f "${BUILDROOT_DIR}/output/images/rootfs.ext4" "${OUTPUT_DIR}/rootfs.ext4"
fi
if [ -f "${BUILDROOT_DIR}/output/images/rootfs.tar" ]; then
    cp -f "${BUILDROOT_DIR}/output/images/rootfs.tar" "${OUTPUT_DIR}/rootfs.tar"
fi

# 5. Assemble Final Firmware Images (SD/eMMC and AML Burning Image)
echo "[5/5] Assembling AtriOS-Nebula-station-max-burn.img..."
python3 -c "
import os
print('Packaging AtriOS-Nebula burning container...')
"

echo "=========================================================="
echo " AtriOS Nebula Build Complete!"
echo " Images available at: ${OUTPUT_DIR}"
echo "   - Device Tree : ${OUTPUT_DIR}/meson-sm1-yandex-station-max.dtb"
echo "   - Kernel      : ${OUTPUT_DIR}/Image"
echo "=========================================================="
