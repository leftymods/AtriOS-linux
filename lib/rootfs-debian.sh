#!/bin/bash
# ==============================================================================
# AtriOS Build System - Super Micro Debian RootFS Generator
# ==============================================================================
# Builds a stripped Debian Bookworm rootfs for the selected architecture with native
# dpkg/apt support, custom AtriOS branding, and pre-staged update packages.

build_debian_micro_rootfs() {
    display_alert "INFO" "Preparing Super Micro Debian RootFS for" "$OS_NAME $RELEASE_NAME"

    check_tool debootstrap
    check_tool mke2fs
    check_tool rsync
    local qemu_binary qemu_name
    qemu_binary=$(resolve_rootfs_qemu_binary "${ARCH:?ARCH must be configured}")
    qemu_name=$(basename -- "$qemu_binary")
    check_tool chroot
    [ -f "${KERNEL_DEB:-}" ] && [ -f "${UBOOT_DEB:-}" ] || \
        exit_with_error "Build current kernel and U-Boot packages before rootfs"

    local cache_dir="$PROJECT_ROOT/cache/rootfs_debian_base"
    local stage_dir="$PROJECT_ROOT/output/rootfs_debian_staging"
    local rootfs_img="$ROOTFS_IMAGE"
    local debian_mirror="${DEBIAN_MIRROR:-https://deb.debian.org/debian}"
    local suite="bookworm"
    local base_packages="ca-certificates,openssl,fish,kmod,util-linux,coreutils,passwd,wireless-regdb,iw,hostname,ncurses-base,ncurses-bin"
    local archive_keyring base_profile
    archive_keyring=$(debian_archive_keyring)
    base_profile=$(debian_base_profile "$suite" "$ARCH" "$debian_mirror" "$base_packages" "$archive_keyring" "$qemu_binary")

    mkdir -p "$PROJECT_ROOT/cache" "$PROJECT_ROOT/output/images"

    # Ensure the installed binfmt registrations are active on the build host
    sudo service binfmt-support status >/dev/null 2>&1 || sudo service binfmt-support start 2>/dev/null || true

    # 1. Bootstrap base if not cached
    if [ ! -f "$cache_dir/.atrios-base-profile" ] || \
       [ "$(cat "$cache_dir/.atrios-base-profile" 2>/dev/null)" != "$base_profile" ]; then
        display_alert "INFO" "Downloading minimal Debian ${suite} ${ARCH} base via debootstrap (first stage)..."
        bootstrap_debian_base "$cache_dir" "$suite" "$ARCH" "$debian_mirror" \
            "$base_packages" "$archive_keyring" "$base_profile" "$qemu_binary"

        display_alert "OK" "Debian base bootstrap completed."
    else
        display_alert "INFO" "Reusing cached Debian base from" "$cache_dir"
    fi

    # 2. Sync to staging directory
    display_alert "INFO" "Preparing staging environment..."
    sudo rm -rf "$stage_dir"
    mkdir -p "$stage_dir"
    sudo rsync -aHAX "$cache_dir/" "$stage_dir/"
    sudo cp "$qemu_binary" "$stage_dir/usr/bin/$qemu_name"

    # 2.5 Apply RootFS Overlay
    if [ -n "${ROOTFS_OVERLAY:-}" ] && [ -d "$PROJECT_ROOT/$ROOTFS_OVERLAY" ]; then
        display_alert "INFO" "Applying RootFS Overlay from" "$ROOTFS_OVERLAY"
        apply_rootfs_overlay "$PROJECT_ROOT/$ROOTFS_OVERLAY" "$stage_dir"
    fi

    install_common_rootfs_services "$stage_dir"

    # Ensure standard Debian UsrMerge symlinks are preserved (lib -> usr/lib, etc.)
    for d in bin sbin lib; do
        if [ -d "$stage_dir/$d" ] && [ ! -L "$stage_dir/$d" ]; then
            sudo cp -a "$stage_dir/$d/." "$stage_dir/usr/$d/"
            sudo rm -rf "$stage_dir/$d"
            sudo ln -s "usr/$d" "$stage_dir/$d"
        fi
    done
    # Debian supplies architecture-specific interpreter paths. These aliases
    # repair the arm64 overlay only; never inject an AArch64 loader into ARM32.
    if [ "$ARCH" = arm64 ]; then
        if [ ! -e "$stage_dir/lib64" ]; then
            sudo ln -s usr/lib "$stage_dir/lib64" 2>/dev/null || true
        fi
        if [ ! -e "$stage_dir/usr/lib/ld-linux-aarch64.so.1" ]; then
            sudo ln -sf aarch64-linux-gnu/ld-linux-aarch64.so.1 "$stage_dir/usr/lib/ld-linux-aarch64.so.1" 2>/dev/null || true
        fi
    fi

    # Debian defaults to its own signing key, absent from the mainline kernel.
    # Select the upstream db/signature pair without issuing host netlink calls.
    sudo chroot "$stage_dir" /bin/sh /etc/init.d/S02regdb select-upstream

    # 3. Configure System & Branding
    display_alert "INFO" "Applying ${OS_NAME} ${RELEASE_NAME} configuration & identity..."

    printf '%s\n' "$BOARD" | sudo tee "$stage_dir/etc/atrios-board" >/dev/null
    printf '%s\n' "$RELEASE_VERSION" | sudo tee "$stage_dir/etc/atrios-version" >/dev/null

    # Generate multi-speaker hardware capability manifest (/etc/atrios/hardware.json)
    sudo mkdir -p "$stage_dir/etc/atrios"
    cat <<HWEOF | sudo tee "$stage_dir/etc/atrios/hardware.json" >/dev/null
{
  "schema": 1,
  "board": "${BOARD}",
  "name": "${BOARD_NAME:-AtriOS Speaker}",
  "vendor": "${VENDOR:-generic}",
  "soc": "${SOC:-unknown}",
  "dtb": "${DTS_NAME}.dtb",
  "display": {
    "type": "${HW_DISPLAY_TYPE:-none}",
    "width": ${HW_DISPLAY_WIDTH:-0},
    "height": ${HW_DISPLAY_HEIGHT:-0}
  },
  "led_ring": {
    "count": ${HW_RING_LEDS:-0}
  },
  "audio": {
    "amp_i2c_bus": ${HW_AMP_I2C_BUS:-2},
    "amp_tweeter_addr": "${HW_AMP_TWEETER_ADDR:-0x2a}",
    "amp_woofer_addr": "${HW_AMP_WOOFER_ADDR:-0x2b}",
    "mic_channels": ${HW_MIC_CHANNELS:-0},
    "capture_device": "${HW_CAPTURE_DEVICE:-hw:0,3}"
  },
  "uarts": {
    "bluetooth": "${HW_BT_UART:-/dev/ttyAML1}",
    "zigbee": "${HW_ZIGBEE_UART:-/dev/ttyAML2}"
  },
  "features": {
    "rotary": ${HW_HAS_ROTARY:-false},
    "als": ${HW_HAS_ALS:-false},
    "zigbee": ${HW_HAS_ZIGBEE:-false},
    "bluetooth": ${HW_HAS_BT:-true}
  }
}
HWEOF

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

    # Preserve Debian's locked base credentials and package-owned system users.
    # Provision custom users/passwords in an explicit provisioning step after
    # overlay application (chroot useradd/chpasswd), never by copying account DBs.

    # Configure default shell for root to fish
    echo "/usr/bin/fish" | sudo tee -a "$stage_dir/etc/shells" >/dev/null
    echo "/bin/fish" | sudo tee -a "$stage_dir/etc/shells" >/dev/null
    sudo sed -i 's|root:x:0:0:root:/root:/bin/bash|root:x:0:0:root:/root:/usr/bin/fish|' "$stage_dir/etc/passwd" 2>/dev/null || true
    sudo sed -i 's|root:x:0:0:root:/root:/bin/sh|root:x:0:0:root:/root:/usr/bin/fish|' "$stage_dir/etc/passwd" 2>/dev/null || true

    # Restore command modes even when an overlay checkout has index mode 0644.
    # Keep certificates, shared libraries and ordinary /etc data non-executable.
    prepare_rootfs_command_modes "$stage_dir"
    normalize_rootfs_startup_text "$stage_dir"

    configure_rootfs_console "$stage_dir"

    # Universal init uses the validated console selected by this board.
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
# BOOT must be mounted before packages or services can update kernel artifacts.
mkdir -p /boot /tmp
if ! mount -a; then
    echo "[init] Failed to mount filesystems from /etc/fstab" >&2
    exit 1
fi

# Apply the validated configured hostname before systemd or the service loop.
if [ -f /etc/init.d/S00hostname ]; then
    /bin/sh /etc/init.d/S00hostname start || echo "[init] Failed to apply hostname" >&2
fi

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

# Read validated console data through the common helper; use the kernel console
# if an older image has no configuration or its serial device is unavailable.
if [ -r /usr/lib/atrios/console.sh ]; then
    . /usr/lib/atrios/console.sh
fi

# Launch the login shell with a controlling TTY inside a supervisor loop.
while true; do
    CONSOLE_PATH=/dev/console
    if command -v atrios_console_path >/dev/null 2>&1; then
        CONSOLE_PATH=$(atrios_console_path)
    fi
    chmod 600 "$CONSOLE_PATH" 2>/dev/null || true

    if [ -x /usr/bin/setsid ]; then
        /usr/bin/setsid -c -w "$DEFAULT_SHELL" -l < "$CONSOLE_PATH" > "$CONSOLE_PATH" 2>&1 || \
        /usr/bin/setsid -c -w /bin/bash -l < "$CONSOLE_PATH" > "$CONSOLE_PATH" 2>&1 || \
        /usr/bin/setsid -c -w /bin/sh -l < "$CONSOLE_PATH" > "$CONSOLE_PATH" 2>&1 || true
    else
        "$DEFAULT_SHELL" -l < "$CONSOLE_PATH" > "$CONSOLE_PATH" 2>&1 || true
    fi
    sleep 1
done
INITEOF
    sudo chmod +x "$stage_dir/sbin/init"
    sudo ln -sf sbin/init "$stage_dir/init"

    # Install only packages just built for this board; never install stale globs.
    display_alert INFO "Installing current kernel and U-Boot packages..."
    sudo rm -rf "$stage_dir/usr/lib/modules"
    sudo mkdir -p "$stage_dir/root/packages"
    for deb in "$KERNEL_DEB" "$UBOOT_DEB"; do
        local deb_base="$(basename "$deb")"
        sudo cp -f "$deb" "$stage_dir/root/packages/$deb_base"
        sudo chroot "$stage_dir" /usr/bin/env ATRIOS_IMAGE_BUILD=1 \
            /usr/bin/dpkg -i "/root/packages/$deb_base"
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

    # Validate the final stripped tree, including terminal data used by fish.
    display_alert "INFO" "Validating RootFS binaries and dynamic loader..."
    sudo cp "$qemu_binary" "$stage_dir/usr/bin/$qemu_name"
    sudo chroot "$stage_dir" /bin/sh -c "/bin/dash -c 'echo Dash test: OK'"
    sudo chroot "$stage_dir" /bin/sh -c "/usr/bin/fish --version"
    validate_rootfs_terminfo "$stage_dir"
    sudo chroot "$stage_dir" /usr/bin/openssl version >/dev/null
    sudo chroot "$stage_dir" /bin/sh -c 'test -s /etc/ssl/certs/ca-certificates.crt'
    sudo chroot "$stage_dir" /bin/sh -c 'test -s /lib/firmware/regulatory.db && test -s /lib/firmware/regulatory.db.p7s'
    sudo chroot "$stage_dir" /bin/sh -c "test -x /sbin/init && echo Init script: OK"

    # Remove qemu binary before packing
    sudo rm -f "$stage_dir/usr/bin/$qemu_name"

    # 7. Create ext4 filesystem image
    display_alert "INFO" "Generating ext4 image (${rootfs_img})..."
    rm -f "$rootfs_img"
    # Sized to 1024MB (matches partition 2 size on disk image)
    mkdir -p "$(dirname "$rootfs_img")"
    truncate -s "$(( ${ROOTFS_PARTITION_SIZE:-2097152} * 512 ))" "$rootfs_img"
    sudo mke2fs -q -F -t ext4 -L "ROOTFS" -d "$stage_dir" "$rootfs_img" >/dev/null

    local final_size="$(stat -c %s "$rootfs_img")"
    display_alert "OK" "Super Micro Debian RootFS created successfully:" "${rootfs_img} (${final_size} bytes)"
}

prepare_rootfs_command_modes() {
    local stage_dir="$1" command_dir
    for command_dir in usr/bin usr/sbin usr/lib/atrios etc/init.d; do
        if [ -d "$stage_dir/$command_dir" ]; then
            sudo find "$stage_dir/$command_dir" -type f -exec chmod a+x {} +
        fi
    done
}

install_common_rootfs_services() {
    local stage_dir="$1"
    sudo install -D -m 755 "$PROJECT_ROOT/lib/runtime/S02regdb" "$stage_dir/etc/init.d/S02regdb"
    sudo install -D -m 755 "$PROJECT_ROOT/lib/runtime/S00hostname" "$stage_dir/etc/init.d/S00hostname"
}


normalize_rootfs_startup_text() {
    local stage_dir="$1"
    sudo python3 "$PROJECT_ROOT/lib/normalize-rootfs-text.py" "$stage_dir"
}

apply_rootfs_overlay() {
    local overlay_dir="$1" stage_dir="$2" account_parent
    # Do not replace or follow a symlink around the protected account directory.
    for account_parent in "$overlay_dir/etc" "$stage_dir/etc"; do
        if [ -L "$account_parent" ] || { [ -e "$account_parent" ] && [ ! -d "$account_parent" ]; }; then
            exit_with_error "Rootfs /etc must be a real directory: $account_parent"
        fi
    done
    # Keep package-created identities (including _apt), authentication and modes.
    sudo rsync -aK --exclude=/etc/passwd --exclude=/etc/group \
        --exclude=/etc/shadow --exclude=/etc/gshadow "$overlay_dir/" "$stage_dir/"
}

validate_rootfs_terminfo() {
    local stage_dir="$1" terminal
    for terminal in linux xterm ansi dumb; do
        sudo chroot "$stage_dir" /usr/bin/infocmp -x "$terminal" >/dev/null || \
            exit_with_error "Required terminfo entry is missing or unreadable: $terminal"
    done
}


configure_rootfs_console() {
    local stage_dir="$1" terminal
    # The board's SERIAL_CONSOLE is also used by boot.cmd/extlinux/uEnv.txt.
    source "$PROJECT_ROOT/lib/runtime/console.sh"
    atrios_console_parse "${SERIAL_CONSOLE:-}" || \
        exit_with_error "Invalid SERIAL_CONSOLE: ${SERIAL_CONSOLE:-<unset>}"
    for terminal in etc/atrios-console etc/securetty usr/lib/atrios/console.sh; do
        [ ! -L "$stage_dir/$terminal" ] || exit_with_error "Console output must not be a symlink: $terminal"
    done
    sudo install -D -m 644 "$PROJECT_ROOT/lib/runtime/console.sh" "$stage_dir/usr/lib/atrios/console.sh"
    printf '%s\n' "$SERIAL_CONSOLE" | sudo tee "$stage_dir/etc/atrios-console" >/dev/null
    sudo chmod 644 "$stage_dir/etc/atrios-console"
    sudo touch "$stage_dir/etc/securetty"
    for terminal in "$ATRIOS_CONSOLE_TTY" console tty0 tty1; do
        if ! sudo grep -qxF -- "$terminal" "$stage_dir/etc/securetty"; then
            printf '%s\n' "$terminal" | sudo tee -a "$stage_dir/etc/securetty" >/dev/null
        fi
    done
    if [ -d "$stage_dir/etc/systemd/system" ]; then
        sudo chroot "$stage_dir" /bin/sh -c \
            'command -v systemctl >/dev/null && exec systemctl enable "$1"' \
            sh "serial-getty@$ATRIOS_CONSOLE_TTY.service" 2>/dev/null || true
    fi
}

# A reviewed keyring may be supplied on hosts whose packaged archive keys predate
# the target Debian suite. Keep signature verification mandatory.
debian_archive_keyring() {
    local keyring="${DEBIAN_ARCHIVE_KEYRING:-/usr/share/keyrings/debian-archive-keyring.gpg}"
    [ ! -L "$keyring" ] && [ -f "$keyring" ] && [ -s "$keyring" ] || \
        exit_with_error "Debian archive keyring must be a non-empty regular file: $keyring"
    realpath -e -- "$keyring"
}

debian_base_profile() {
    local suite="$1" architecture="$2" mirror="$3" packages="$4" keyring="$5" qemu="${6:-}" fingerprint qemu_fingerprint
    fingerprint=$(sha256sum < "$keyring")
    fingerprint="${fingerprint%% *}"
    printf '%s|%s|%s|%s|keyring-sha256=%s' "$suite" "$architecture" "$mirror" "$packages" "$fingerprint"
    if [ -n "$qemu" ]; then
        qemu_fingerprint=$(sha256sum < "$qemu")
        printf '|qemu=%s|qemu-sha256=%s' "$(basename -- "$qemu")" "${qemu_fingerprint%% *}"
    fi
    printf '\n'
}

# Never destroy a previous authenticated base just because a newer bootstrap
# failed (network, keyring, package or second-stage errors).
bootstrap_debian_base() {
    local cache_dir="$1" suite="$2" architecture="$3" mirror="$4" packages="$5" keyring="$6" profile="$7"
    local parent work previous had_previous=0 qemu_binary qemu_name
    qemu_binary=$(resolve_rootfs_qemu_binary "$architecture" "${8:-${ROOTFS_QEMU_BINARY:-}}")
    qemu_name=$(basename -- "$qemu_binary")
    [ ! -L "$cache_dir" ] && { [ ! -e "$cache_dir" ] || [ -d "$cache_dir" ]; } || \
        exit_with_error "Debian base cache must be a real directory"
    parent=$(dirname -- "$cache_dir")
    mkdir -p "$parent"
    work=$(mktemp -d "$parent/.rootfs-debian-base.XXXXXX")
    if sudo debootstrap --keyring="$keyring" --variant=minbase --include="$packages" \
        --arch="$architecture" --foreign "$suite" "$work" "$mirror" && \
        sudo cp "$qemu_binary" "$work/usr/bin/$qemu_name" && \
        sudo chroot "$work" /debootstrap/debootstrap --second-stage && \
        printf '%s\n' "$profile" | sudo tee "$work/.atrios-base-profile" >/dev/null; then
        previous="$work.previous"
        if [ -e "$cache_dir" ]; then
            if ! sudo mv -T -- "$cache_dir" "$previous"; then
                sudo rm -rf -- "$work"
                return 1
            fi
            had_previous=1
        fi
        if ! sudo mv -T -- "$work" "$cache_dir"; then
            if [ "$had_previous" -eq 1 ]; then
                sudo mv -T -- "$previous" "$cache_dir" || \
                    exit_with_error "Cannot restore previous Debian base cache: $previous"
            fi
            sudo rm -rf -- "$work"
            return 1
        fi
        [ "$had_previous" -eq 0 ] || sudo rm -rf -- "$previous"
    else
        sudo rm -rf -- "$work"
        return 1
    fi
}

# Debian architecture names differ from qemu-user architecture names.
resolve_rootfs_qemu_binary() {
    local architecture="$1" selected="${2:-${ROOTFS_QEMU_BINARY:-}}" command_name
    if [ -z "$selected" ]; then
        case "$architecture" in
            arm64) command_name=qemu-aarch64-static ;;
            armhf|armel) command_name=qemu-arm-static ;;
            *) exit_with_error "Set ROOTFS_QEMU_BINARY for unsupported Debian architecture: $architecture" ;;
        esac
        selected=$(command -v "$command_name") || \
            exit_with_error "Required target emulator is unavailable: $command_name"
    elif [[ "$selected" != /* ]]; then
        selected="${PROJECT_ROOT:?PROJECT_ROOT is required for a relative ROOTFS_QEMU_BINARY}/$selected"
    fi
    [ ! -L "$selected" ] && [ -f "$selected" ] && [ -s "$selected" ] && [ -x "$selected" ] || \
        exit_with_error "ROOTFS_QEMU_BINARY must be a non-empty executable regular file: $selected"
    realpath -e -- "$selected"
}
