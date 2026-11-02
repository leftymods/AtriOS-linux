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

    # Ensure binfmt_misc for arm64 is active on host
    sudo service binfmt-support status >/dev/null 2>&1 || sudo service binfmt-support start 2>/dev/null || true

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

    # 2.5 Apply RootFS Overlay
    if [ -n "${ROOTFS_OVERLAY:-}" ] && [ -d "$PROJECT_ROOT/$ROOTFS_OVERLAY" ]; then
        display_alert "INFO" "Applying RootFS Overlay from" "$ROOTFS_OVERLAY"
        sudo rsync -aK "$PROJECT_ROOT/$ROOTFS_OVERLAY/" "$stage_dir/"
    fi

    # Ensure standard Debian UsrMerge symlinks are preserved (lib -> usr/lib, etc.)
    for d in bin sbin lib; do
        if [ -d "$stage_dir/$d" ] && [ ! -L "$stage_dir/$d" ]; then
            sudo cp -a "$stage_dir/$d/." "$stage_dir/usr/$d/"
            sudo rm -rf "$stage_dir/$d"
            sudo ln -s "usr/$d" "$stage_dir/$d"
        fi
    done
    if [ ! -e "$stage_dir/lib64" ]; then
        sudo ln -s usr/lib "$stage_dir/lib64" 2>/dev/null || true
    fi
    if [ ! -e "$stage_dir/usr/lib/ld-linux-aarch64.so.1" ]; then
        sudo ln -sf aarch64-linux-gnu/ld-linux-aarch64.so.1 "$stage_dir/usr/lib/ld-linux-aarch64.so.1" 2>/dev/null || true
    fi

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

    # Configure default shell for root to fish
    echo "/usr/bin/fish" | sudo tee -a "$stage_dir/etc/shells" >/dev/null
    echo "/bin/fish" | sudo tee -a "$stage_dir/etc/shells" >/dev/null
    sudo sed -i 's|root:x:0:0:root:/root:/bin/bash|root:x:0:0:root:/root:/usr/bin/fish|' "$stage_dir/etc/passwd" 2>/dev/null || true
    sudo sed -i 's|root:x:0:0:root:/root:/bin/sh|root:x:0:0:root:/root:/usr/bin/fish|' "$stage_dir/etc/passwd" 2>/dev/null || true

    # Ensure permissions and line endings for atrios, fish, and init scripts
    sudo chmod +x "$stage_dir/usr/bin/atrios" "$stage_dir/usr/bin/fish" 2>/dev/null || true
    if [ -d "$stage_dir/usr/lib/atrios" ]; then
        sudo chmod -R +x "$stage_dir/usr/lib/atrios" 2>/dev/null || true
    fi
    if [ -d "$stage_dir/etc/init.d" ]; then
        sudo chmod +x "$stage_dir/etc/init.d/"* 2>/dev/null || true
    fi
    sudo dos2unix "$stage_dir/usr/bin/atrios" "$stage_dir/etc/fish/config.fish" 2>/dev/null || true

    # Ensure /etc/securetty allows serial console login
    cat <<EOF | sudo tee -a "$stage_dir/etc/securetty" >/dev/null
ttyAML0
console
tty0
tty1
EOF

    # Enable serial console ttyAML0 in systemd
    if [ -d "$stage_dir/etc/systemd/system" ]; then
        sudo chroot "$stage_dir" /bin/sh -c "systemctl enable serial-getty@ttyAML0.service" 2>/dev/null || true
    fi

    # Universal /sbin/init & /init wrapper: silences dmesg console spam and launches fish shell on ttyAML0
    cat <<'INITEOF' | sudo tee "$stage_dir/sbin/init" >/dev/null
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export TERM=linux
export HOME=/root

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts /dev/shm
mount -t devpts devpts /dev/pts 2>/dev/null || true
mount -t tmpfs tmpfs /dev/shm 2>/dev/null || true

# Silence kernel console logging so runtime dmesg doesn't pollute the interactive shell
dmesg -n 1 2>/dev/null || true
echo 1 > /proc/sys/kernel/printk 2>/dev/null || true

# If systemd is installed, launch it
if [ -x /lib/systemd/systemd ] && [ ! -f /etc/nosystemd ]; then
    exec /lib/systemd/systemd "$@"
fi

# Start AtriOS hardware and system services from /etc/init.d
if [ -d /etc/init.d ]; then
    for s in /etc/init.d/S*; do
        [ -x "$s" ] || continue
        echo "[init] Starting $(basename "$s")..."
        "$s" start 2>/dev/null || true
    done
fi

# Print banner to console
cat /etc/issue 2>/dev/null || echo "Welcome to AtriOS-aml!"

# Determine preferred shell (fish > bash > sh)
if [ -x /usr/bin/fish ]; then
    DEFAULT_SHELL="/usr/bin/fish"
elif [ -x /bin/fish ]; then
    DEFAULT_SHELL="/bin/fish"
elif [ -x /bin/bash ]; then
    DEFAULT_SHELL="/bin/bash"
else
    DEFAULT_SHELL="/bin/sh"
fi

# Launch interactive root login shell with proper controlling TTY inside a supervisor loop
while true; do
    CONSOLE_DEV="ttyAML0"
    [ -e "/dev/$CONSOLE_DEV" ] || CONSOLE_DEV="console"

    chmod 666 "/dev/$CONSOLE_DEV" 2>/dev/null || true

    if [ -x /usr/bin/setsid ]; then
        /usr/bin/setsid -c -w "$DEFAULT_SHELL" -l < "/dev/$CONSOLE_DEV" > "/dev/$CONSOLE_DEV" 2>&1 || \
        /usr/bin/setsid -c -w /bin/bash -l < "/dev/$CONSOLE_DEV" > "/dev/$CONSOLE_DEV" 2>&1 || \
        /usr/bin/setsid -c -w /bin/sh -l < "/dev/$CONSOLE_DEV" > "/dev/$CONSOLE_DEV" 2>&1 || true
    else
        "$DEFAULT_SHELL" -l < "/dev/$CONSOLE_DEV" > "/dev/$CONSOLE_DEV" 2>&1 || true
    fi
    sleep 1
done
INITEOF
    sudo chmod +x "$stage_dir/sbin/init"
    sudo ln -sf sbin/init "$stage_dir/init"

    # 4. Install Kernel Modules
    display_alert "INFO" "Installing kernel modules into rootfs..."
    local kver="7.3.1-AtriOS-aml"
    local mod_dest="$stage_dir/usr/lib/modules/${kver}"
    sudo mkdir -p "$mod_dest"
    
    local kernel_cache="$PROJECT_ROOT/cache/kernel"
    if [ -d "$kernel_cache" ]; then
        display_alert "INFO" "Running modules_install from kernel tree..."
        sudo make -C "$kernel_cache" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$stage_dir" modules_install 2>/dev/null || true
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
                "$stage_dir/var/cache/apt"/*.bin \
                "$stage_dir/var/lib/apt/lists"/* \
                "$stage_dir/tmp"/* \
                "$stage_dir/var/tmp"/*

    # Validate executables inside chroot before stripping
    display_alert "INFO" "Validating RootFS binaries and dynamic loader..."
    sudo cp /usr/bin/qemu-aarch64-static "$stage_dir/usr/bin/"
    sudo chroot "$stage_dir" /bin/sh -c "/bin/dash -c 'echo Dash test: OK'"
    sudo chroot "$stage_dir" /bin/sh -c "/usr/bin/fish --version"
    sudo chroot "$stage_dir" /bin/sh -c "test -x /sbin/init && echo Init script: OK"

    # Remove qemu binary before packing
    sudo rm -f "$stage_dir/usr/bin/qemu-aarch64-static"

    # 7. Create ext4 filesystem image
    display_alert "INFO" "Generating ext4 image (${rootfs_img})..."
    rm -f "$rootfs_img"
    # Sized to 1024MB (matches partition 2 size on disk image)
    truncate -s 1024M "$rootfs_img"
    sudo mke2fs -q -F -t ext4 -L "ROOTFS" -d "$stage_dir" "$rootfs_img" >/dev/null

    local final_size="$(stat -c %s "$rootfs_img")"
    display_alert "OK" "Super Micro Debian RootFS created successfully:" "${rootfs_img} (${final_size} bytes)"
}
