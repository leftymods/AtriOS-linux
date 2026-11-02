#!/bin/bash
# ==============================================================================
# AtriOS Build System - Package Management Generator (.deb)
# ==============================================================================
# Generates .deb packages for U-Boot and Linux Kernel with on-device postinst hooks.

build_u_boot_package() {
    display_alert "INFO" "Building U-Boot .deb package for" "$BOARD ($BOARD_NAME)"
    
    local pkg_name="atrios-u-boot-${BOARD}"
    local pkg_version="1.0"
    local pkg_arch="arm64"
    local pkg_dir="$PROJECT_ROOT/output/packages/${pkg_name}_${pkg_version}_${pkg_arch}"
    local deb_out="$PROJECT_ROOT/output/packages/${pkg_name}_${pkg_version}_${pkg_arch}.deb"
    local bootloader_src="$PROJECT_ROOT/$BOOTLOADER_BIN"

    [ -f "$bootloader_src" ] || exit_with_error "Bootloader binary not found: $bootloader_src"

    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/DEBIAN"
    mkdir -p "$pkg_dir/usr/lib/u-boot/${BOARD}"

    # Copy binary
    cp "$bootloader_src" "$pkg_dir/usr/lib/u-boot/${BOARD}/u-boot.bin.sd.bin"

    # Control file
    cat > "$pkg_dir/DEBIAN/control" <<CTRL
Package: ${pkg_name}
Version: ${pkg_version}
Section: admin
Priority: optional
Architecture: ${pkg_arch}
Maintainer: AtriOS Project <dev@atrios.org>
Description: AtriOS U-Boot bootloader and FIP for ${BOARD_NAME}
 Provides raw bootloader image and automatic eMMC partition flasher.
CTRL

    # Post-install script: flashes to eMMC on-device
    cat > "$pkg_dir/DEBIAN/postinst" << 'PST'
#!/bin/sh
set -e

UBOOT_SRC="/usr/lib/u-boot/station-max/u-boot.bin.sd.bin"
TARGET_DEV="/dev/mmcblk0"

if [ -f "$UBOOT_SRC" ] && [ -b "$TARGET_DEV" ]; then
    echo "[AtriOS] Updating bootloader on ${TARGET_DEV}..."
    dd if="$UBOOT_SRC" of="$TARGET_DEV" bs=1 count=442 conv=notrunc status=none
    dd if="$UBOOT_SRC" of="$TARGET_DEV" bs=512 skip=1 seek=1 conv=notrunc status=none
    sync
    echo "[AtriOS] U-Boot successfully updated on ${TARGET_DEV}."
else
    echo "[AtriOS] Warning: Target device ${TARGET_DEV} not present, bootloader files staged only."
fi

exit 0
PST
    chmod 755 "$pkg_dir/DEBIAN/postinst"

    # Build package
    mkdir -p "$PROJECT_ROOT/output/packages"
    dpkg-deb --build "$pkg_dir" "$deb_out" >/dev/null
    rm -rf "$pkg_dir"

    display_alert "OK" "U-Boot package created:" "$deb_out"
}

build_kernel_package() {
    display_alert "INFO" "Building Linux Kernel .deb package for" "$BOARD ($BOARD_NAME)"

    local kver="7.3.1"
    local pkg_name="atrios-linux-image-${kver}"
    local pkg_version="${kver}-1"
    local pkg_arch="arm64"
    local pkg_dir="$PROJECT_ROOT/output/packages/${pkg_name}_${pkg_version}_${pkg_arch}"
    local deb_out="$PROJECT_ROOT/output/packages/${pkg_name}_${pkg_version}_${pkg_arch}.deb"

    local kernel_img="$KERNEL_IMAGE"
    local dtb_file="$PROJECT_ROOT/$DTB_IMAGE"

    [ -f "$kernel_img" ] || exit_with_error "Kernel image not found: $kernel_img"
    [ -f "$dtb_file" ] || exit_with_error "DTB not found: $dtb_file"

    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/DEBIAN"
    mkdir -p "$pkg_dir/boot"
    mkdir -p "$pkg_dir/usr/lib/modules/${kver}"

    # Copy Kernel Image and DTB
    cp "$kernel_img" "$pkg_dir/boot/Image"
    cp "$dtb_file" "$pkg_dir/boot/${DTS_NAME}.dtb"

    # Copy modules if compiled
    local mod_src="$PROJECT_ROOT/cache/kernel/drivers/atrios"
    if [ -d "$mod_src" ]; then
        find "$mod_src" -name "*.ko" -exec cp {} "$pkg_dir/usr/lib/modules/${kver}/" \; 2>/dev/null || true
    fi

    # Control file
    cat > "$pkg_dir/DEBIAN/control" <<CTRL
Package: ${pkg_name}
Version: ${pkg_version}
Section: kernel
Priority: optional
Architecture: ${pkg_arch}
Maintainer: AtriOS Project <dev@atrios.org>
Description: AtriOS Linux Kernel and DTB for ${BOARD_NAME}
 Kernel release ${kver} with hardware device tree and peripheral drivers.
CTRL

    # Post-install script: updates /boot and executes depmod
    cat > "$pkg_dir/DEBIAN/postinst" << 'PST'
#!/bin/sh
set -e

echo "[AtriOS] Updating kernel artifacts in /boot..."
if [ -d /boot ]; then
    cp -f /boot/meson-sm1-yandex-station-max.dtb /boot/dtb.img 2>/dev/null || true
    sync
fi

if command -v depmod >/dev/null 2>&1; then
    echo "[AtriOS] Running depmod..."
    depmod -a || true
fi

echo "[AtriOS] Kernel update complete."
exit 0
PST
    chmod 755 "$pkg_dir/DEBIAN/postinst"

    # Build package
    mkdir -p "$PROJECT_ROOT/output/packages"
    dpkg-deb --build "$pkg_dir" "$deb_out" >/dev/null
    rm -rf "$pkg_dir"

    display_alert "OK" "Kernel package created:" "$deb_out"
}

build_all_packages() {
    build_u_boot_package
    build_kernel_package
}
