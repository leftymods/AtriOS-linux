#!/bin/bash
set -e
PROJECT_ROOT="/home/lmods/MainlineAtriOS"
cd "$PROJECT_ROOT/cache/kernel"

echo "=== Copying defconfig and DTS and running olddefconfig ==="
cp "$PROJECT_ROOT/board/yandex/station2/mainline/linux_7.0_yndxsttion53_defconfig" .config
cp "$PROJECT_ROOT/board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts" arch/arm64/boot/dts/amlogic/meson-sm1-yandex-station-max.dts
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make olddefconfig

echo "=== Checking critical configs ==="
grep "CONFIG_PINCTRL_MESON_G12A" .config
grep "CONFIG_LOCALVERSION" .config
grep "CONFIG_SERIAL_MESON" .config

echo "=== Compiling Kernel and DTBs ==="
make -j16 Image dtbs modules

echo "=== Copying kernel artifacts to output/images ==="
mkdir -p "$PROJECT_ROOT/output/images"
cp arch/arm64/boot/Image "$PROJECT_ROOT/output/images/Image"
cp arch/arm64/boot/dts/amlogic/meson-sm1-yandex-station-max.dtb "$PROJECT_ROOT/output/images/meson-sm1-yandex-station-max.dtb"

echo "=== Kernel Build Complete ==="
