#!/bin/bash
# ==============================================================================
# AtriOS Build System - Super Micro Debian RootFS Generator
# ==============================================================================
# Builds a stripped, ultra-lightweight Debian Bookworm arm64 rootfs with native
# dpkg/apt support, custom AtriOS branding, and pre-staged update packages.

build_debian_micro_rootfs() {
    display_alert "INFO" "Preparing Super Micro Debian RootFS for" "$OS_NAME $RELEASE_NAME"

    check_tool debootstrap
    check_tool mke2fs
    check_tool rsync

    local cache_dir="$PROJECT_ROOT/cache/rootfs_debian_base"
    local stage_dir="$PROJECT_ROOT/output/rootfs_debian_staging"
    local rootfs_img="$PROJECT_ROOT/output/images/rootfs-debian-micro.ext4"
    local debian_mirror="${DEBIAN_MIRROR:-http://deb.debian.org/debian}"
    local suite="bookworm"

    mkdir -p "$PROJECT_ROOT/cache" "$PROJECT_ROOT/output/images"

    # 1. Bootstrap base if not cached
    if [ ! -f "$cache_dir/etc/debian_version" ]; then
        display_alert "INFO" "Downloading minimal Debian ${suite} arm64 base via debootstrap (first stage)..."
        sudo rm -rf "$cache_dir"
        mkdir -p "$cache_dir"
        sudo debootstrap --no-check-gpg --variant=minbase --arch=arm64 --foreign "$suite" "$cache_dir" "$debian_mirror"
        
        display_alert "INFO" "Configuring target architecture via qemu-aarch64-static (second stage)..."
        sudo cp /usr/bin/qemu-aarch64-static "$cache_dir/usr/bin/"
        sudo chroot "$cache_dir" /debootstrap/debootstrap --second-stage
        
        display_alert "OK" "Debian base bootstrap completed."
    else
        display_alert "INFO" "Reusing cached Debian base from" "$cache_dir"
    fi

    # 2. Sync to staging directory
    display_alert "INFO" "Preparing staging environment..."
    sudo rm -rf "$stage_dir"
    mkdir -p "$stage_dir"
    sudo rsync -aHAX "$cache_dir/" "$stage_dir/"
    sudo cp /usr/bin/qemu-aarch64-static "$stage_dir/usr/bin/"

    # 3. Configure System & Branding
    display_alert "INFO" "Applying ${OS_NAME} ${RELEASE_NAME} configuration & identity..."

    # Hostname & Hosts
    echo "$OS_NAME" | sudo tee "$stage_dir/etc/hostname" >/dev/null
    cat <<EOF | sudo tee "$stage_dir/etc/hosts" >/dev/null
127.0.0.1   localhost
127.0.1.1   ${OS_NAME}
EOF

    # Issue banner
    cat <<EOF | sudo tee "$stage_dir/etc/issue" >/dev/null
Welcome to ${OS_NAME} ${RELEASE_NAME} [${BOARD_NAME}]
Kernel \r on an \m (\l)

EOF
    sudo cp "$stage_dir/etc/issue" "$stage_dir/etc/issue.net"

    # OS Release
    cat <<EOF | sudo tee "$stage_dir/etc/os-release" >/dev/null
NAME="${OS_NAME}"
PRETTY_NAME="${OS_NAME} ${RELEASE_NAME} (v${RELEASE_VERSION})"
ID=atrios
ID_LIKE=debian
VERSION="${RELEASE_VERSION} (${RELEASE_NAME})"
VERSION_ID="${RELEASE_VERSION}"
HOME_URL="https://github.com/leftymods/AtriOS-linux"
SUPPORT_URL="https://github.com/leftymods/AtriOS-linux/issues"
EOF

    # fstab
    cat <<EOF | sudo tee "$stage_dir/etc/fstab" >/dev/null
# <file system> <mount point>   <type>  <options>       <dump>  <pass>
LABEL=BOOT      /boot           vfat    defaults,sync   0       2
LABEL=ROOTFS    /               ext4    defaults,noatime 0      1
proc            /proc           proc    defaults        0       0
sysfs           /sys            sysfs   defaults        0       0
tmpfs           /tmp            tmpfs   defaults        0       0
EOF

    # Set root password to empty / root
    sudo chroot "$stage_dir" /bin/sh -c "echo 'root:root' | chpasswd" 2>/dev/null || true

    # Enable serial console ttyAML0
    if [ -d "$stage_dir/etc/systemd/system" ]; then
        sudo chroot "$stage_dir" /bin/sh -c "systemctl enable serial-getty@ttyAML0.service" 2>/dev/null || true
    fi

    # 4. Install Kernel Modules
    display_alert "INFO" "Installing kernel modules into rootfs..."
    local kver="7.3.1"
    local mod_dest="$stage_dir/usr/lib/modules/${kver}"
    sudo mkdir -p "$mod_dest"
    
    local mod_src="$PROJECT_ROOT/cache/kernel/drivers/atrios"
    if [ -d "$mod_src" ]; then
        sudo find "$mod_src" -name "*.ko" -exec cp {} "$mod_dest/" \; 2>/dev/null || true
    fi
    sudo ln -sf usr/lib/modules "$stage_dir/lib/modules" 2>/dev/null || true

    # 5. Pre-stage and install .deb update packages
    display_alert "INFO" "Staging .deb packages for on-device updates..."
    sudo mkdir -p "$stage_dir/root/packages"
    sudo cp -f "$PROJECT_ROOT/output/packages"/*.deb "$stage_dir/root/packages/" 2>/dev/null || true

    # Install the packages into rootfs database
    display_alert "INFO" "Pre-registering update packages in dpkg database..."
    for deb in "$stage_dir/root/packages"/*.deb; do
        [ -f "$deb" ] || continue
        local deb_base="$(basename "$deb")"
        sudo chroot "$stage_dir" /usr/bin/dpkg -i "/root/packages/$deb_base" 2>/dev/null || true
    done

    # 6. Aggressive Micro-Stripping
    display_alert "INFO" "Performing super-micro optimization (stripping unnecessary files)..."
    sudo rm -rf "$stage_dir/usr/share/doc" \
                "$stage_dir/usr/share/man" \
                "$stage_dir/usr/share/info" \
                "$stage_dir/usr/share/groff" \
                "$stage_dir/usr/share/lintian" \
                "$stage_dir/usr/share/linda" \
                "$stage_dir/var/cache/apt/archives"/*.deb \
                "$stage_dir/var/lib/apt/lists"/* \
                "$stage_dir/tmp"/* \
                "$stage_dir/var/tmp"/*

    # Remove qemu binary before packing
    sudo rm -f "$stage_dir/usr/bin/qemu-aarch64-static"

    # 7. Create ext4 filesystem image
    display_alert "INFO" "Generating ext4 image (${rootfs_img})..."
    rm -f "$rootfs_img"
    # Sized to 512MB (plenty for micro-debian and update packages)
    truncate -s 512M "$rootfs_img"
    sudo mke2fs -q -F -t ext4 -L "ROOTFS" -d "$stage_dir" "$rootfs_img" >/dev/null

    local final_size="$(stat -c %s "$rootfs_img")"
    display_alert "OK" "Super Micro Debian RootFS created successfully:" "${rootfs_img} (${final_size} bytes)"
}
