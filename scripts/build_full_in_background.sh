#!/bin/bash
set -e
exec > /home/lmods/MainlineAtriOS/build_bg.log 2>&1

echo "[$(date)] === STARTING BUILD ==="
PROJECT_ROOT="/home/lmods/MainlineAtriOS"
cd "$PROJECT_ROOT"

echo "[$(date)] 1. Building Kernel Image, DTB, Modules..."
bash scripts/build_kernel_and_image.sh

echo "[$(date)] 2. Rebuilding RootFS..."
rm -f output/images/rootfs-debian-micro.ext4
./compile.sh BOARD=yndxsttion53 TARGET=rootfs

echo "[$(date)] 3. Building eMMC Image..."
./compile.sh BOARD=yndxsttion53 TARGET=image

echo "[$(date)] 4. Copying to Windows Desktop origfw..."
cp -f output/images/AtriOS-Nebula-yndxsttion53-emmc.img /mnt/c/Users/ggala/Desktop/origfw/AtriOS-Nebula-yndxsttion53-emmc.img

echo "[$(date)] 5. Committing to git..."
git add drivers/atrios/ board/yandex/ lib/ scripts/
git commit -m "fix(drivers, dts): rotary-volume polling fallback, Gowin FPGA firmware embedding, I2C aliases, RTL8822CS Wi-Fi & BT" || true
git push origin mainline || true

echo "[$(date)] === BUILD COMPLETE AND SUCCESSFUL ==="
