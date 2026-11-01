#!/usr/bin/env python3
import os
import sys
import struct
import shutil
import subprocess
import gzip

def pad_to_page(data: bytes, page_size: int = 2048) -> bytes:
    pad = (page_size - (len(data) % page_size)) % page_size
    return data + b'\0' * pad

def create_boot_partition(kernel_gz_path: str, dtb_path: str, out_path: str, cmdline: str):
    with open(kernel_gz_path, 'rb') as f:
        kernel_data = f.read()
    with open(dtb_path, 'rb') as f:
        dtb_data = f.read()

    k_size = len(kernel_data)
    r_size = 0
    s_size = len(dtb_data)

    k_addr = 0x01080000
    r_addr = 0x01000000
    s_addr = 0x00f00000
    tags_addr = 0x00000100
    page_size = 2048
    header_ver = 1
    os_ver = 0

    magic = b'ANDROID!'
    # 8s (magic) + 10I: k_size, k_addr, r_size, r_addr, s_size, s_addr, tags_addr, page_size, header_ver, os_ver
    hdr_core = struct.pack('<8s10I', magic, k_size, k_addr, r_size, r_addr, s_size, s_addr, tags_addr, page_size, header_ver, os_ver)

    name = b'\0' * 16
    cmdline_bytes = cmdline.encode('latin1')[:512].ljust(512, b'\0')
    id_bytes = b'\0' * 32
    extra_cmdline = b'\0' * 1024
    recovery_dtbo_size = 0
    recovery_dtbo_offset = 0
    header_size = 1648

    hdr_v1_extra = struct.pack('<16s512s32s1024sIQI', name, cmdline_bytes, id_bytes, extra_cmdline, recovery_dtbo_size, recovery_dtbo_offset, header_size)
    header = hdr_core + hdr_v1_extra
    header_padded = pad_to_page(header, page_size)

    kernel_padded = pad_to_page(kernel_data, page_size)
    dtb_padded = pad_to_page(dtb_data, page_size)

    total_boot = header_padded + kernel_padded + dtb_padded
    # Ensure minimum 16MB or standard size
    if len(total_boot) < 16 * 1024 * 1024:
        total_boot += b'\0' * (16 * 1024 * 1024 - len(total_boot))

    with open(out_path, 'wb') as f:
        f.write(total_boot)
    print(f"[+] boot.PARTITION created: {out_path} ({len(total_boot)} bytes)")

def main():
    repo_root = "/home/lmods/MainlineAtriOS"
    out_dir = os.path.join(repo_root, "output/images")
    staging_dir = os.path.join(repo_root, "output/burn_staging")
    upgrade_dir = os.path.join(repo_root, "device/yandex/station_max/upgrade")
    ampack_bin = "/home/lmods/AmlogicKitchen/bin/ampack"
    dtb_tool_bin = "/home/lmods/AmlogicKitchen/bin/dtbTool"

    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(staging_dir, exist_ok=True)

    print("=== AtriOS Nebula AML Burn Packager ===")

    # 1. Check Kernel Image
    kernel_img = os.path.join(out_dir, "Image")
    if not os.path.exists(kernel_img):
        kernel_img = "/home/lmods/linux-leftymods/arch/arm64/boot/Image"
    assert os.path.exists(kernel_img), f"Kernel Image not found at {kernel_img}"

    # Compress to Image.gz
    kernel_gz = os.path.join(out_dir, "Image.gz")
    print(f"[*] Compressing {kernel_img} -> {kernel_gz}...")
    with open(kernel_img, 'rb') as f_in, gzip.open(kernel_gz, 'wb', compresslevel=9) as f_out:
        shutil.copyfileobj(f_in, f_out)

    # 2. Check DTB
    dtb_file = os.path.join(out_dir, "meson-sm1-yandex-station-max.dtb")
    assert os.path.exists(dtb_file), f"DTB not found at {dtb_file}"

    # 3. Create Multi-DTB Table
    dtb_tmp_dir = "/tmp/dtb_combine"
    os.makedirs(dtb_tmp_dir, exist_ok=True)
    shutil.copy2(dtb_file, os.path.join(dtb_tmp_dir, "meson-sm1-yandex-station-max.dtb"))
    multi_dtb = os.path.join(staging_dir, "_aml_dtb.PARTITION")
    print("[*] Generating Amlogic multi-DTB...")
    subprocess.run([dtb_tool_bin, "-o", multi_dtb, dtb_tmp_dir + "/"], check=True)
    shutil.copy2(multi_dtb, os.path.join(staging_dir, "meson1.dtb"))
    shutil.copy2(dtb_file, os.path.join(staging_dir, "sm1_s905x3_yandex.dtb"))

    # 4. Create boot.PARTITION
    boot_part = os.path.join(staging_dir, "boot.PARTITION")
    cmdline = "root=/dev/mmcblk0p18 rootfstype=ext4 rw rootwait skip_initramfs init=/sbin/init androidboot.selinux=permissive enforcing=0 console=ttyAML0,115200 no_console_suspend earlyprintk=aml-uart,0xff803000"
    print("[*] Creating boot.PARTITION...")
    create_boot_partition(kernel_gz, dtb_file, boot_part, cmdline)
    shutil.copy2(boot_part, os.path.join(staging_dir, "recovery.PARTITION"))

    # 5. Copy Bootloader & Platform Assets
    print("[*] Copying Bootloader & Board Assets...")
    bootloader_src = os.path.join(upgrade_dir, "bootloader.img")
    uboot_sd_src = os.path.join(upgrade_dir, "u-boot.bin.sd.bin")
    platform_conf = os.path.join(upgrade_dir, "platform.conf")
    aml_sdc_burn_ini = os.path.join(upgrade_dir, "aml_sdc_burn.ini")
    logo_part = os.path.join(upgrade_dir, "logo.PARTITION")

    # USB Bootloader binaries
    usb_bl2 = os.path.join(upgrade_dir, "u-boot.bin.encrypt.usb.bl2")
    usb_tpl = os.path.join(upgrade_dir, "u-boot.bin.encrypt.usb.tpl")
    if not os.path.exists(usb_bl2):
        usb_bl2 = bootloader_src
    if not os.path.exists(usb_tpl):
        usb_tpl = bootloader_src

    shutil.copy2(usb_bl2, os.path.join(staging_dir, "DDR.USB"))
    shutil.copy2(usb_tpl, os.path.join(staging_dir, "UBOOT.USB"))
    shutil.copy2(bootloader_src, os.path.join(staging_dir, "bootloader.PARTITION"))
    shutil.copy2(uboot_sd_src, os.path.join(staging_dir, "aml_sdc_burn.UBOOT"))
    shutil.copy2(platform_conf, os.path.join(staging_dir, "platform.conf"))
    shutil.copy2(aml_sdc_burn_ini, os.path.join(staging_dir, "aml_sdc_burn.ini"))
    shutil.copy2(logo_part, os.path.join(staging_dir, "logo.PARTITION"))

    # 6. Check rootfs.ext4
    rootfs_ext4 = os.path.join(repo_root, "buildroot/output/images/rootfs.ext4")
    if os.path.exists(rootfs_ext4):
        print(f"[*] Found rootfs.ext4: {rootfs_ext4}")
        shutil.copy2(rootfs_ext4, os.path.join(staging_dir, "system.PARTITION"))
    else:
        print("[!] rootfs.ext4 not yet completed by Buildroot, waiting or creating temporary placeholder...")
        # Check if fallback or previous system partition exists
        ref_sys = "/tmp/ampack_test/system.PARTITION"
        if os.path.exists(ref_sys):
            shutil.copy2(ref_sys, os.path.join(staging_dir, "system.PARTITION"))

    # 7. Pack AML Burning Image
    target_burn_img = os.path.join(out_dir, "AtriOS-Nebula-station-max-burn.img")
    print(f"[*] Packing {staging_dir} into {target_burn_img}...")
    subprocess.run([ampack_bin, "pack", "--out-ver", "v2", "--out-align", "4", staging_dir, target_burn_img], check=True)
    print(f"[+] Successfully generated: {target_burn_img}")

    # Copy to Windows desktop
    win_dest = "/mnt/c/Users/ggala/Desktop/origfw/AtriOS-Nebula-station-max-burn.img"
    if os.path.exists("/mnt/c/Users/ggala/Desktop/origfw"):
        print(f"[*] Copying image to Windows: {win_dest}...")
        shutil.copy2(target_burn_img, win_dest)
        print(f"[+] Transferred to Windows successfully!")

if __name__ == "__main__":
    main()
