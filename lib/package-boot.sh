# Embedded in generated maintainer scripts; POSIX sh, no writes at source time.
atrios_root_disk() {
    case "$1" in
        /dev/mmcblk*p2)
            number=${1#/dev/mmcblk}; number=${number%p2}
            case "$number" in ''|*[!0-9]*) return 1 ;; esac
            printf '%s\n' "${1%p2}" ;;

        *) echo "Refusing update: root is not an eMMC partition 2: $1" >&2; return 1 ;;
    esac
}

# SOURCE may be /dev/root with no device node; sysfs retains the real partition.
atrios_resolve_root_source() {
    root_source=$(findmnt -nro SOURCE /) || return 1
    canonical_source=$(readlink -f "$root_source" 2>/dev/null) || canonical_source=""
    if atrios_root_disk "$canonical_source" >/dev/null 2>&1; then
        printf '%s\n' "$canonical_source"
        return 0
    fi
    root_devno=$(findmnt -nro MAJ:MIN /) || return 1
    case "$root_devno" in
        *[!0-9:]*|:*|*:) echo "Invalid root device number: $root_devno" >&2; return 1 ;;
    esac
    case "$root_devno" in *:*:*) return 1 ;; *:*) ;; *) return 1 ;; esac
    root_sysfs=$(readlink -f "/sys/dev/block/$root_devno") || return 1
    [ -n "$root_sysfs" ] || return 1
    root_source="/dev/${root_sysfs##*/}"
    atrios_root_disk "$root_source" >/dev/null || return 1
    printf '%s\n' "$root_source"
}

atrios_boot_target() {
    actual_board=$(cat /etc/atrios-board) || return 1
    [ "$actual_board" = "$BOARD" ] || {
        echo "Package targets $BOARD, installed board is $actual_board" >&2; return 1;
    }
    root_source=$(atrios_resolve_root_source) || return 1
    TARGET_DEV=$(atrios_root_disk "$root_source") || return 1
    disk_name=${TARGET_DEV##*/}
    [ -b "$TARGET_DEV" ] && [ "$(cat "/sys/class/block/$disk_name/device/type")" = MMC ] || {
        echo "Refusing update: $TARGET_DEV is not an eMMC device" >&2; return 1;
    }
    BOOT_DEV="${TARGET_DEV}p1"
    [ -b "$BOOT_DEV" ] && [ "$(blkid -s LABEL -o value "$BOOT_DEV")" = BOOT ] || {
        echo "Refusing update: missing BOOT partition on $TARGET_DEV" >&2; return 1;
    }
}

atrios_mount_boot() {
    mkdir -p /boot
    if mountpoint -q /boot; then
        boot_source=$(findmnt -nro SOURCE --target /boot) || return 1
        boot_source=$(readlink -f "$boot_source") || return 1
        [ "$boot_source" = "$BOOT_DEV" ] || {
            echo "Refusing update: /boot is mounted from $boot_source, expected $BOOT_DEV" >&2; return 1;
        }
    else
        mount "$BOOT_DEV" /boot || return 1
    fi
}

atrios_check_bootloader_size() {
    case "$1:$2" in *[!0-9:]*|:*|*:) return 1 ;; esac
    [ "$1" -ge 512 ] && [ "$1" -le "$(($2 * 512))" ] || {
        echo "Bootloader does not fit before the first partition" >&2; return 1;
    }
}
