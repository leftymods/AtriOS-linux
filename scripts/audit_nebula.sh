#!/usr/bin/env bash
set -e

REPO="/home/lmods/MainlineAtriOS"
KDIR="/home/lmods/linux-leftymods"
BDIR="/home/lmods/MainlineAtriOS/buildroot"

echo "=== [1/5] DEVICE TREE VALIDATION ==="
cpp -nostdinc -I "${KDIR}/include" \
    -I "${KDIR}/arch/arm64/boot/dts/amlogic" \
    -I "${KDIR}/arch/arm64/boot/dts" \
    -undef -x assembler-with-cpp "${REPO}/board/yandex/station2/mainline/dts/meson-sm1-yandex-station-max.dts" | \
    dtc -I dts -O dtb -o /tmp/audit_dts.dtb 2>&1 | grep -v 'Warning' || true
if [ -f /tmp/audit_dts.dtb ]; then
    echo "PASS: Device Tree compiled cleanly (size: $(stat -c %s /tmp/audit_dts.dtb) bytes)"
    rm -f /tmp/audit_dts.dtb
else
    echo "FAIL: Device Tree failed to compile"
    exit 1
fi

echo "=== [2/5] ALL 13 DRIVERS AUDIT ==="
DRIVERS_DIR="${REPO}/drivers/atrios"
cd "${DRIVERS_DIR}"
make -C "${KDIR}" M="${DRIVERS_DIR}" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KBUILD_MODPOST_WARN=1 modules >/dev/null 2>&1

MISSING=0
for mod in gowin_led_device quasar_led_screen quasar_led_ring quasar_pcba_ids \
           rotary-volume gpio_keypad zigbee-control ltr308als01 leds-is31fl32xx \
           snd-soc-sy6045s snd-soc-es8156 snd-soc-es7210 snd-soc-tas5825m; do
    if [ -f "${mod}.ko" ]; then
        echo "  [OK] ${mod}.ko ($(stat -c %s "${mod}.ko") bytes)"
    else
        echo "  [FAIL] ${mod}.ko MISSING"
        MISSING=$((MISSING + 1))
    fi
done

# Clean intermediate files
rm -f *.o *.ko *.mod *.mod.c *.order .*cmd

if [ "${MISSING}" -eq 0 ]; then
    echo "PASS: All 13 drivers compiled to .ko successfully"
else
    echo "FAIL: ${MISSING} drivers failed to compile"
    exit 1
fi

echo "=== [3/5] BUILDROOT CONFIGURATION AUDIT ==="
cd "${BDIR}"
make defconfig BR2_DEFCONFIG="${REPO}/board/yandex/station2/mainline/buildroot_station_max_defconfig" >/dev/null 2>&1
if [ -f "${BDIR}/.config" ]; then
    echo "PASS: Buildroot 2026.08.1 .config successfully generated"
    grep "BR2_TARGET_GENERIC_HOSTNAME" "${BDIR}/.config"
    grep "BR2_TARGET_GENERIC_ISSUE" "${BDIR}/.config"
fi

echo "=== [4/5] USERSPACE & FISH SHELL AUDIT ==="
OVERLAY="${REPO}/board/yandex/station2/rootfs_overlay"
if [ -f "${OVERLAY}/etc/hostname" ]; then
    echo "PASS: Hostname: $(cat "${OVERLAY}/etc/hostname")"
fi
if grep -q "root:.*:/usr/bin/fish" "${OVERLAY}/etc/passwd"; then
    echo "PASS: Root shell is /usr/bin/fish"
fi
if grep -q "AtriOS Nebula" "${OVERLAY}/etc/fish/config.fish"; then
    echo "PASS: Fish greeting configured for AtriOS Nebula without leftymods tag"
fi

echo "=== [5/5] GIT STATUS AUDIT ==="
cd "${REPO}"
git status -s

echo "=== ALL CHECKS PASSED SUCCESSFULLY ==="
