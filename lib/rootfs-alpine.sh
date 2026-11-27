#!/bin/bash
# ==============================================================================
# AtriOS Build System - Alpine Linux Micro RootFS Generator
# ==============================================================================
# Builds a compact Alpine Linux (musl + BusyBox) rootfs for older or lower-RAM
# speakers (e.g. Station Lite / Mini) with apk support, modular AtriOS services,
# and minimal flash footprint (~20-40 MB vs ~150+ MB for Debian).

alpine_arch_for_board() {
    case "${1:-arm64}" in
        arm64|aarch64) echo "aarch64" ;;
        armhf)         echo "armhf" ;;
        armv7|armel)   echo "armv7" ;;
        x86_64|amd64)  echo "x86_64" ;;
        *) exit_with_error "Unsupported Alpine architecture: ${1:-<unset>}" ;;
    esac
}

alpine_minirootfs_url() {
    local suite="${1:-3.20}"
    local release="${2:-3.20.3}"
    local arch="$3"
    local mirror="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"
    printf '%s/v%s/releases/%s/alpine-minirootfs-%s-%s.tar.gz' "$mirror" "$suite" "$arch" "$release" "$arch"
}

build_alpine_micro_rootfs() {
    display_alert "INFO" "Preparing Alpine Linux Micro RootFS for" "$OS_NAME $RELEASE_NAME ($BOARD)"

    check_tool curl
    check_tool tar
    check_tool mke2fs
    check_tool rsync
    check_tool chroot

    local qemu_binary qemu_name
    qemu_binary=$(resolve_rootfs_qemu_binary "${ARCH:?ARCH must be configured}")
    qemu_name=$(basename -- "$qemu_binary")

    local alpine_arch suite release mirror tarball_url sha256_url
    alpine_arch=$(alpine_arch_for_board "$ARCH")
    suite="${ROOTFS_SUITE:-3.20}"
    release="${ALPINE_MINIROOTFS_VERSION:-${suite}.3}"
    mirror="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"
    tarball_url=$(alpine_minirootfs_url "$suite" "$release" "$alpine_arch")
    sha256_url="${tarball_url}.sha256"

    local cache_dir="$PROJECT_ROOT/cache/rootfs_alpine_${suite}_${alpine_arch}"
    local tarball_cache="$PROJECT_ROOT/cache/alpine-minirootfs-${release}-${alpine_arch}.tar.gz"
    local stage_dir="$PROJECT_ROOT/output/rootfs_alpine_staging"
    local rootfs_img="$ROOTFS_IMAGE"
    local base_packages="${ROOTFS_BASE_PACKAGES:-alpine-baselayout busybox ca-certificates iw kmod wireless-regdb musl}"

    mkdir -p "$PROJECT_ROOT/cache" "$PROJECT_ROOT/output/images"
    sudo service binfmt-support status >/dev/null 2>&1 || sudo service binfmt-support start 2>/dev/null || true

    # 1. Download & verify official Alpine minirootfs tarball if not cached
    if [ ! -f "$tarball_cache" ]; then
        display_alert "INFO" "Downloading Alpine ${release} (${alpine_arch}) minirootfs..."
        local tmp_tar="${tarball_cache}.tmp"
        local tmp_sha="${tarball_cache}.sha256.tmp"
        curl -fsSL "$tarball_url" -o "$tmp_tar"
        curl -fsSL "$sha256_url" -o "$tmp_sha"
        local expected_sha actual_sha
        expected_sha="$(awk '{print $1}' "$tmp_sha")"
        actual_sha="$(sha256sum "$tmp_tar" | awk '{print $1}')"
        if [ -z "$expected_sha" ] || [ "$expected_sha" != "$actual_sha" ]; then
            rm -f "$tmp_tar" "$tmp_sha"
            exit_with_error "Alpine minirootfs SHA-256 mismatch (expected $expected_sha, got $actual_sha)"
        fi
        mv -f "$tmp_tar" "$tarball_cache"
        rm -f "$tmp_sha"
        display_alert "OK" "Verified Alpine minirootfs SHA-256:" "$actual_sha"
    fi

    # 2. Extract into cached base if needed
    local base_profile="alpine|${suite}|${release}|${alpine_arch}|${base_packages}"
    if [ ! -f "$cache_dir/.atrios-base-profile" ] || \
       [ "$(cat "$cache_dir/.atrios-base-profile" 2>/dev/null)" != "$base_profile" ]; then
        local work
        work=$(mktemp -d "$PROJECT_ROOT/cache/.rootfs-alpine-base.XXXXXX")
        sudo tar -xzf "$tarball_cache" -C "$work"
        sudo cp "$qemu_binary" "$work/usr/bin/$qemu_name"
        printf '%s/v%s/main\n%s/v%s/community\n' "$mirror" "$suite" "$mirror" "$suite" | \
            sudo tee "$work/etc/apk/repositories" >/dev/null
        if [ -f /etc/resolv.conf ]; then
            sudo cp /etc/resolv.conf "$work/etc/resolv.conf"
        fi
        # shellcheck disable=SC2086
        sudo chroot "$work" /sbin/apk add --no-cache $base_packages
        sudo rm -f "$work/usr/bin/$qemu_name" "$work/etc/resolv.conf"
        printf '%s\n' "$base_profile" | sudo tee "$work/.atrios-base-profile" >/dev/null
        sudo rm -rf "$cache_dir"
        sudo mv -T "$work" "$cache_dir"
        display_alert "OK" "Alpine base rootfs cached at" "$cache_dir"
    else
        display_alert "INFO" "Reusing cached Alpine base from" "$cache_dir"
    fi

    # 3. Sync cached base to staging directory
    sudo rm -rf "$stage_dir"
    mkdir -p "$stage_dir"
    sudo rsync -aHAX "$cache_dir/" "$stage_dir/"

    # 4. Apply board RootFS overlay + common services
    if [ -n "${ROOTFS_OVERLAY:-}" ] && [ -d "$PROJECT_ROOT/$ROOTFS_OVERLAY" ]; then
        display_alert "INFO" "Applying RootFS Overlay from" "$ROOTFS_OVERLAY"
        apply_rootfs_overlay "$PROJECT_ROOT/$ROOTFS_OVERLAY" "$stage_dir"
    fi
    install_common_rootfs_services "$stage_dir"

    # 5. Prune disabled modules & write hardware + modules manifests
    prune_disabled_rootfs_modules "$stage_dir"
    write_hardware_manifest "$stage_dir"
    write_modules_manifest "$stage_dir"

    # 6. Configure identity, branding, fstab, console, and lightweight /sbin/init
    printf '%s\n' "$BOARD" | sudo tee "$stage_dir/etc/atrios-board" >/dev/null
    printf '%s\n' "$RELEASE_VERSION" | sudo tee "$stage_dir/etc/atrios-version" >/dev/null
    echo "$OS_NAME" | sudo tee "$stage_dir/etc/hostname" >/dev/null
    cat <<EOF | sudo tee "$stage_dir/etc/hosts" >/dev/null
127.0.0.1   localhost
127.0.1.1   ${OS_NAME}
EOF
    cat <<EOF | sudo tee "$stage_dir/etc/os-release" >/dev/null
NAME="${OS_NAME}"
PRETTY_NAME="${OS_NAME} ${RELEASE_NAME} Alpine Lite (v${RELEASE_VERSION})"
ID=atrios
ID_LIKE=alpine
VERSION="${RELEASE_VERSION} (${RELEASE_NAME})"
VERSION_ID="${RELEASE_VERSION}"
ATRIOS_PROFILE="${ATRIOS_PROFILE:-lite}"
ATRIOS_APP_FLAVOR="${ATRIOS_APP_FLAVOR:-lite}"
HOME_URL="https://github.com/leftymods/AtriOS-linux"
EOF
    cat <<EOF | sudo tee "$stage_dir/etc/fstab" >/dev/null
LABEL=BOOT      /boot           vfat    defaults,sync    0       2
LABEL=ROOTFS    /               ext4    defaults,noatime 0       1
proc            /proc           proc    defaults         0       0
sysfs           /sys            sysfs   defaults         0       0
tmpfs           /tmp            tmpfs   defaults         0       0
EOF

    prepare_rootfs_command_modes "$stage_dir"
    normalize_rootfs_startup_text "$stage_dir"
    configure_rootfs_console "$stage_dir"

    cat <<'INITEOF' | sudo tee "$stage_dir/sbin/init" >/dev/null
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export TERM=linux
export HOME=/root

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts /dev/shm /boot /tmp
mount -t devpts devpts /dev/pts 2>/dev/null || true
mount -t tmpfs tmpfs /dev/shm 2>/dev/null || true
mount -a || { echo "[init] mount -a failed" >&2; exit 1; }

[ -f /etc/init.d/S00hostname ] && /bin/sh /etc/init.d/S00hostname start || true
dmesg -n 1 2>/dev/null || true

if [ -d /etc/init.d ]; then
    for s in /etc/init.d/S*; do
        [ -x "$s" ] || continue
        echo "[init] Starting $(basename "$s")..."
        "$s" start 2>/dev/null || true
    done
fi

if [ -r /usr/lib/atrios/console.sh ]; then
    . /usr/lib/atrios/console.sh
fi
while true; do
    CONSOLE_PATH=/dev/console
    if command -v atrios_console_path >/dev/null 2>&1; then
        CONSOLE_PATH=$(atrios_console_path)
    fi
    /bin/sh -l < "$CONSOLE_PATH" > "$CONSOLE_PATH" 2>&1 || true
    sleep 1
done
INITEOF
    sudo chmod +x "$stage_dir/sbin/init"
    sudo ln -sf sbin/init "$stage_dir/init"

    # 7. Strip caches/docs for minimal footprint
    sudo rm -rf "$stage_dir/usr/share/doc" \
                "$stage_dir/usr/share/man" \
                "$stage_dir/usr/share/info" \
                "$stage_dir/var/cache/apk"/* \
                "$stage_dir/tmp"/* \
                "$stage_dir/var/tmp"/*

    # 8. Build ext4 image sized to board's ROOTFS_PARTITION_SIZE
    display_alert "INFO" "Generating Alpine ext4 image (${rootfs_img})..."
    rm -f "$rootfs_img"
    mkdir -p "$(dirname "$rootfs_img")"
    truncate -s "$(( ${ROOTFS_PARTITION_SIZE:-524288} * 512 ))" "$rootfs_img"
    sudo mke2fs -q -F -t ext4 -L "ROOTFS" -d "$stage_dir" "$rootfs_img" >/dev/null

    local final_size
    final_size="$(stat -c %s "$rootfs_img")"
    display_alert "OK" "Alpine Micro RootFS created successfully:" "${rootfs_img} (${final_size} bytes)"
}
