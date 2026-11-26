#!/bin/bash
# Build packages from the same kernel/U-Boot artifacts used by the image.

kernel_release() {
    resolve_kernel_build_profile
    [ -f "$KERNEL_BUILD_DIR/Makefile" ] || exit_with_error "Build the selected kernel profile before packaging"
    make -s -C "$KERNEL_BUILD_DIR" ARCH="$KERNEL_BUILD_ARCH" CROSS_COMPILE="$CROSS_COMPILE" kernelrelease
}

package_postinst_header() {
    printf "#!/bin/sh\nset -eu\nBOARD='%s'\n" "$BOARD"
    cat "$PROJECT_ROOT/lib/package-boot.sh"
}

build_u_boot_package() {
    local pkg_name="atrios-u-boot-${BOARD}" pkg_version="1.0" pkg_arch="$ARCH"
    local pkg_dir="$PROJECT_ROOT/output/packages/${pkg_name}_${pkg_version}_${pkg_arch}"
    UBOOT_DEB="${pkg_dir}.deb"
    [ -s "$BOOTLOADER_BIN" ] || exit_with_error "Bootloader binary not found: $BOOTLOADER_BIN"
    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/DEBIAN" "$pkg_dir/usr/lib/u-boot/$BOARD"
    cp "$BOOTLOADER_BIN" "$pkg_dir/usr/lib/u-boot/$BOARD/u-boot.bin.sd.bin"
    cat > "$pkg_dir/DEBIAN/control" <<CTRL
Package: $pkg_name
Version: $pkg_version
Section: admin
Priority: optional
Architecture: $pkg_arch
Maintainer: AtriOS Project <dev@atrios.org>
Depends: util-linux, coreutils
Description: AtriOS U-Boot bootloader and FIP for $BOARD_NAME
CTRL
    package_postinst_header > "$pkg_dir/DEBIAN/postinst"
    cat >> "$pkg_dir/DEBIAN/postinst" <<'PST'
[ "${ATRIOS_IMAGE_BUILD:-0}" = 1 ] && exit 0
UBOOT_SRC="/usr/lib/u-boot/$BOARD/u-boot.bin.sd.bin"
[ -s "$UBOOT_SRC" ] || exit 1
atrios_boot_target
first_sector=$(cat "/sys/class/block/${TARGET_DEV##*/}p1/start")
atrios_check_bootloader_size "$(stat -c %s "$UBOOT_SRC")" "$first_sector"
echo "[AtriOS] Updating bootloader on $TARGET_DEV"
# Preserve bytes 440..511 (disk signature, partition table and MBR signature).
dd if="$UBOOT_SRC" of="$TARGET_DEV" bs=1 count=440 conv=notrunc,fsync status=none
dd if="$UBOOT_SRC" of="$TARGET_DEV" bs=512 skip=1 seek=1 conv=notrunc,fsync status=none
sync
PST
    chmod 755 "$pkg_dir/DEBIAN/postinst"
    dpkg-deb --root-owner-group --build "$pkg_dir" "$UBOOT_DEB" >/dev/null
    rm -rf "$pkg_dir"
    display_alert OK "U-Boot package created:" "$UBOOT_DEB"
}

build_kernel_package() {
    local krel pkg_version pkg_name="atrios-linux-image-${BOARD}" pkg_arch="$ARCH"
    resolve_kernel_build_profile
    krel=$(kernel_release)
    [[ "$krel" =~ ^[0-9][a-zA-Z0-9.+~-]*$ ]] || exit_with_error "Invalid kernel release: $krel"
    pkg_version="${krel}-1"
    local pkg_dir="$PROJECT_ROOT/output/packages/${pkg_name}_${pkg_version}_${pkg_arch}"
    KERNEL_DEB="${pkg_dir}.deb"
    local profile_image="$KERNEL_BUILD_DIR/arch/$KERNEL_BUILD_ARCH/boot/$KERNEL_BUILD_IMAGE_TARGET"
    local profile_dtb="$KERNEL_BUILD_DIR/arch/$KERNEL_BUILD_ARCH/boot/dts/${KERNEL_BUILD_DTS_SUBDIR:+$KERNEL_BUILD_DTS_SUBDIR/}${DTS_NAME}.dtb"
    [ -s "$profile_image" ] || exit_with_error "Kernel Image not found for selected profile: $profile_image"
    [ -s "$profile_dtb" ] || exit_with_error "DTB not found for selected profile: $profile_dtb"
    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/DEBIAN" "$pkg_dir/usr/lib/atrios-kernel/$krel"
    cp "$profile_image" "$pkg_dir/usr/lib/atrios-kernel/$krel/Image"
    cp "$profile_dtb" "$pkg_dir/usr/lib/atrios-kernel/$krel/$DTS_NAME.dtb"
    make -C "$KERNEL_BUILD_DIR" ARCH="$KERNEL_BUILD_ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
        INSTALL_MOD_PATH="$pkg_dir/usr" DEPMOD=true modules_install
    local mod_dir="$pkg_dir/usr/lib/modules/$krel"
    [ -d "$mod_dir" ] || exit_with_error "modules_install did not install $krel"
    rm -f "$mod_dir/build" "$mod_dir/source"
    depmod -b "$pkg_dir/usr" "$krel"
    cat > "$pkg_dir/DEBIAN/control" <<CTRL
Package: $pkg_name
Version: $pkg_version
Section: kernel
Priority: optional
Architecture: $pkg_arch
Maintainer: AtriOS Project <dev@atrios.org>
Depends: kmod, util-linux, coreutils
Description: AtriOS kernel, DTB and modules for $BOARD_NAME ($krel)
CTRL
    package_postinst_header > "$pkg_dir/DEBIAN/postinst"
    printf "KERNEL_RELEASE='%s'\nDTS_NAME='%s'\n" "$krel" "$DTS_NAME" >> "$pkg_dir/DEBIAN/postinst"
    cat >> "$pkg_dir/DEBIAN/postinst" <<'PST'
if [ "${ATRIOS_IMAGE_BUILD:-0}" != 1 ]; then
    atrios_boot_target
    atrios_mount_boot
fi
mkdir -p /boot
payload="/usr/lib/atrios-kernel/$KERNEL_RELEASE"
for name in Image "$DTS_NAME.dtb"; do
    [ ! -f "/boot/$name" ] || cp -p "/boot/$name" "/boot/$name.bak"
    cp "$payload/$name" "/boot/$name.new"
    mv -f "/boot/$name.new" "/boot/$name"
done
cp "/boot/$DTS_NAME.dtb" /boot/dtb.img
depmod -a "$KERNEL_RELEASE"
sync
PST
    chmod 755 "$pkg_dir/DEBIAN/postinst"
    dpkg-deb --root-owner-group --build "$pkg_dir" "$KERNEL_DEB" >/dev/null
    rm -rf "$pkg_dir"
    display_alert OK "Kernel package created:" "$KERNEL_DEB"
}

build_all_packages() {
    check_tool dpkg-deb
    check_tool depmod
    build_kernel
    bash "$PROJECT_ROOT/scripts/build_mainline_uboot.sh"
    build_u_boot_package
    build_kernel_package
}
