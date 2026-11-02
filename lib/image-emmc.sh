#!/bin/bash
# ==============================================================================
# AtriOS Build System - eMMC Image Assembly
# ==============================================================================

build_emmc_image() {
    display_alert "INFO" "Starting eMMC image creation for" "${OS_NAME} ${RELEASE_NAME} on ${BOARD_NAME}"

    check_tool truncate
    check_tool mkfs.vfat
    check_tool mcopy
    check_tool sfdisk
    check_tool dd
    check_tool mkimage

    local out_dir="$PROJECT_ROOT/output/images"
    local img_path="$out_dir/${IMAGE_FILENAME}"
    local win_dest="/mnt/c/Users/ggala/Desktop/origfw/${IMAGE_FILENAME}"
    
    local boot_staging="/tmp/atrios_boot_staging"
    local boot_vfat="/tmp/atrios_boot.vfat"
    
    local bootloader_full="$PROJECT_ROOT/$BOOTLOADER_BIN"
    local kernel_full="$KERNEL_IMAGE"
    local dtb_full="$PROJECT_ROOT/$DTB_IMAGE"
    local rootfs_full="$PROJECT_ROOT/$ROOTFS_IMAGE"

    # Validation
    [ -f "$bootloader_full" ] || exit_with_error "Bootloader binary not found: $bootloader_full"
    [ -f "$kernel_full" ] || exit_with_error "Kernel image not found: $kernel_full"
    [ -f "$dtb_full" ] || exit_with_error "Device tree not found: $dtb_full"
    [ -f "$rootfs_full" ] || exit_with_error "RootFS image not found: $rootfs_full"

    # 1. Prepare Boot Partition Staging
    display_alert "INFO" "Staging kernel, DTB and ${OS_NAME} ${RELEASE_NAME} boot scripts..."
    rm -rf "$boot_staging"
    mkdir -p "$boot_staging/extlinux"

    cp "$kernel_full" "$boot_staging/Image"
    cp "$dtb_full" "$boot_staging/${DTS_NAME}.dtb"
    cp "$dtb_full" "$boot_staging/dtb.img"

    # boot.cmd
    cat > "$boot_staging/boot.cmd" <<EOF
echo '=== Starting ${OS_NAME} ${RELEASE_NAME} from eMMC ==='
setenv fdt_high 0xffffffffffffffff
setenv initrd_high 0xffffffffffffffff
setenv bootargs 'root=LABEL=ROOTFS rootwait rw console=${SERIAL_CONSOLE} earlycon=aml_uart,0xff803000 init=/sbin/init'
if test -z '\${kernel_addr_r}'; then setenv kernel_addr_r 0x08080000; fi
if test -z '\${fdt_addr_r}'; then setenv fdt_addr_r 0x08008000; fi

for dev in mmc; do
    for part in 1 2; do
        if load \${dev} 0:\${part} \${kernel_addr_r} Image; then
            load \${dev} 0:\${part} \${fdt_addr_r} ${DTS_NAME}.dtb
            booti \${kernel_addr_r} - \${fdt_addr_r}
        fi
    done
done
EOF

    mkimage -A arm -O linux -T script -C none -d "$boot_staging/boot.cmd" "$boot_staging/boot.scr" >/dev/null
    cp "$boot_staging/boot.scr" "$boot_staging/aml_autoscript"
    cp "$boot_staging/boot.scr" "$boot_staging/s905_autoscript"

    # extlinux.conf
    cat > "$boot_staging/extlinux/extlinux.conf" <<EOF
LABEL ${OS_NAME}-${RELEASE_NAME}
    LINUX /Image
    FDT /${DTS_NAME}.dtb
    APPEND root=LABEL=ROOTFS rootwait rw console=${SERIAL_CONSOLE} earlycon=aml_uart,0xff803000 init=/sbin/init
EOF

    # uEnv.txt
    cat > "$boot_staging/uEnv.txt" <<EOF
dtb_name=/${DTS_NAME}.dtb
fdt_high=0xffffffffffffffff
initrd_high=0xffffffffffffffff
bootargs=root=LABEL=ROOTFS rootwait rw console=${SERIAL_CONSOLE} earlycon=aml_uart,0xff803000 init=/sbin/init
EOF

    # 2. Build 128MB FAT32 BOOT Partition
    display_alert "INFO" "Creating 128MB FAT32 BOOT partition..."
    rm -f "$boot_vfat"
    truncate -s 128M "$boot_vfat"
    mkfs.vfat -F 32 -n "BOOT" "$boot_vfat" >/dev/null
    mcopy -i "$boot_vfat" -s "$boot_staging"/* ::/

    # 3. Disk Geometry & Allocation
    local sector_size=512
    local p1_start=${BOOT_PARTITION_START:-32768}       # 16 MB
    local p1_size=${BOOT_PARTITION_SIZE:-262144}        # 128 MB
    local p2_start=${ROOTFS_PARTITION_START:-294912}     # 144 MB
    local p2_size=${ROOTFS_PARTITION_SIZE:-2097152}      # 1024 MB (1.0 GB)
    local total_sectors=$((p2_start + p2_size + 2048))

    display_alert "INFO" "Allocating disk image (${total_sectors} sectors / ~1.14 GB)..."
    mkdir -p "$out_dir"
    rm -f "$img_path"
    truncate -s $((total_sectors * sector_size)) "$img_path"

    # 4. Partition Table via sfdisk
    display_alert "INFO" "Writing DOS MBR partition table..."
    local sfdisk_input="label: dos
label-id: 0x4a7e8b12
unit: sectors

${img_path}1 : start=${p1_start}, size=${p1_size}, type=c, bootable
${img_path}2 : start=${p2_start}, size=${p2_size}, type=83
"
    echo "$sfdisk_input" | sfdisk "$img_path" >/dev/null

    # 5. Burn Partitions
    display_alert "INFO" "Writing BOOT partition (FAT32)..."
    dd if="$boot_vfat" of="$img_path" bs="$sector_size" seek="$p1_start" conv=notrunc status=none

    display_alert "INFO" "Writing ROOTFS partition (ext4)..."
    dd if="$rootfs_full" of="$img_path" bs="$sector_size" seek="$p2_start" conv=notrunc status=none

    # 6. Burn Bootloader (MBR record in sector 0, rest at sector 1)
    display_alert "INFO" "Injecting bootloader (${bootloader_full})..."
    dd if="$bootloader_full" of="$img_path" bs=1 count=442 conv=notrunc status=none
    dd if="$bootloader_full" of="$img_path" bs="$sector_size" skip=1 seek=1 conv=notrunc status=none

    # Clean temporary files
    rm -rf "$boot_staging" "$boot_vfat"

    display_alert "OK" "eMMC image built successfully:" "${img_path}"
    display_alert "INFO" "Image size:" "$(stat -c %s "$img_path") bytes"

    # 7. Copy to Windows Desktop
    if [ -d "/mnt/c/Users/ggala/Desktop/origfw" ]; then
        display_alert "INFO" "Transferring image to Windows:" "${win_dest}..."
        cp "$img_path" "$win_dest"
        display_alert "OK" "Transferred to Windows successfully:" "${win_dest}"
    fi
}
