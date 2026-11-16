#!/bin/bash
# Assemble a bounded disk image; publish only after every write succeeds.
validate_image_geometry() {
    local bootloader_bytes="$1" rootfs_bytes="$2" p1_start="$3" p1_size="$4" p2_start="$5" p2_size="$6" value
    for value in "$@"; do
        [[ "$value" =~ ^[0-9]+$ ]] && [ "$value" -gt 0 ] || return 1
    done
    (( p1_start >= 1 && p1_start + p1_size <= p2_start )) || return 1
    (( bootloader_bytes >= 512 && bootloader_bytes <= p1_start * 512 )) || return 1
    (( rootfs_bytes <= p2_size * 512 )) || return 1
}

build_emmc_image() (
    set -euo pipefail
    local tool
    for tool in truncate mkfs.vfat mcopy sfdisk dd mkimage stat mktemp; do check_tool "$tool"; done
    local out_dir="$PROJECT_ROOT/output/images"
    local img_path="$out_dir/$IMAGE_FILENAME"
    local p1_start="${BOOT_PARTITION_START:-32768}" p1_size="${BOOT_PARTITION_SIZE:-262144}"
    local p2_start="${ROOTFS_PARTITION_START:-294912}" p2_size="${ROOTFS_PARTITION_SIZE:-2097152}"
    local artifact
    for artifact in "$BOOTLOADER_BIN" "$KERNEL_IMAGE" "$DTB_IMAGE" "$ROOTFS_IMAGE"; do
        [ -s "$artifact" ] || exit_with_error "Required image artifact missing: $artifact"
    done
    validate_image_geometry "$(stat -c %s "$BOOTLOADER_BIN")" "$(stat -c %s "$ROOTFS_IMAGE")" \
        "$p1_start" "$p1_size" "$p2_start" "$p2_size" || \
        exit_with_error "Invalid disk geometry or artifacts exceed their partition bounds"
    mkdir -p "$out_dir"
    local work_dir
    work_dir=$(mktemp -d "$out_dir/.emmc-build.XXXXXXXX")
    trap 'rm -rf -- "$work_dir"' EXIT
    local boot_staging="$work_dir/boot" boot_vfat="$work_dir/boot.vfat" image_tmp="$work_dir/image.img"
    mkdir -p "$boot_staging/extlinux"
    cp "$KERNEL_IMAGE" "$boot_staging/Image"
    cp "$DTB_IMAGE" "$boot_staging/$DTS_NAME.dtb"
    cp "$DTB_IMAGE" "$boot_staging/dtb.img"
    cat > "$boot_staging/boot.cmd" <<EOF
echo 'Starting ${OS_NAME} ${RELEASE_NAME}'
setenv fdt_high 0x20000000
setenv initrd_high 0x20000000
setenv bootargs 'root=PARTUUID=4a7e8b12-02 rootwait rw console=${SERIAL_CONSOLE} loglevel=4 init=/sbin/init'
if test -z "\${kernel_addr_r}"; then setenv kernel_addr_r 0x08080000; fi
if test -z "\${fdt_addr_r}"; then setenv fdt_addr_r 0x08008000; fi
for devnum in 0 1 2; do
    if load mmc \${devnum}:1 \${kernel_addr_r} Image; then
        if load mmc \${devnum}:1 \${fdt_addr_r} ${DTS_NAME}.dtb; then
            booti \${kernel_addr_r} - \${fdt_addr_r}
        fi
    fi
done
EOF
    mkimage -A arm -O linux -T script -C none -d "$boot_staging/boot.cmd" "$boot_staging/boot.scr" >/dev/null
    cp "$boot_staging/boot.scr" "$boot_staging/aml_autoscript"
    cp "$boot_staging/boot.scr" "$boot_staging/s905_autoscript"
    cat > "$boot_staging/extlinux/extlinux.conf" <<EOF
LABEL ${OS_NAME}-${RELEASE_NAME}
    LINUX /Image
    FDT /${DTS_NAME}.dtb
    APPEND root=PARTUUID=4a7e8b12-02 rootwait rw console=${SERIAL_CONSOLE} loglevel=4 init=/sbin/init
EOF
    cat > "$boot_staging/uEnv.txt" <<EOF
dtb_name=/${DTS_NAME}.dtb
fdt_high=0x20000000
initrd_high=0x20000000
bootargs=root=PARTUUID=4a7e8b12-02 rootwait rw console=${SERIAL_CONSOLE} loglevel=4 init=/sbin/init
EOF
    truncate -s "$((p1_size * 512))" "$boot_vfat"
    mkfs.vfat -F 32 -n BOOT "$boot_vfat" >/dev/null
    mcopy -i "$boot_vfat" -s "$boot_staging"/* ::/
    truncate -s "$(((p2_start + p2_size + 2048) * 512))" "$image_tmp"
    sfdisk "$image_tmp" >/dev/null <<EOF
label: dos
label-id: 0x4a7e8b12
unit: sectors

start=$p1_start, size=$p1_size, type=c, bootable
start=$p2_start, size=$p2_size, type=83
EOF
    dd if="$boot_vfat" of="$image_tmp" bs=512 seek="$p1_start" conv=notrunc status=none
    dd if="$ROOTFS_IMAGE" of="$image_tmp" bs=512 seek="$p2_start" conv=notrunc status=none
    dd if="$BOOTLOADER_BIN" of="$image_tmp" bs=1 count=440 conv=notrunc status=none
    dd if="$BOOTLOADER_BIN" of="$image_tmp" bs=512 skip=1 seek=1 conv=notrunc status=none
    mv -f "$image_tmp" "$img_path"
    cp "$img_path" "$work_dir/emmc.img"
    mv -f "$work_dir/emmc.img" "$out_dir/emmc.img"
    sha256sum "$img_path" > "$img_path.sha256"
    display_alert OK "eMMC image built successfully:" "$img_path"
)
