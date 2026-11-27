#!/bin/bash
# ==============================================================================
# AtriOS Build System - Configuration Loader
# ==============================================================================

load_board_config() {
    local target_board="${1:-$BOARD}"
    local conf_file="$PROJECT_ROOT/config/boards/${target_board}.conf"
    local system_conf="$PROJECT_ROOT/config/system.conf"
    local release_version_override="${RELEASE_VERSION:-}"

    # 1. Load System & Release Configuration
    if [ -f "$system_conf" ]; then
        # shellcheck source=/dev/null
        source "$system_conf"
    fi

    # RELEASE selects the name; preserve an explicit caller version override.
    OS_NAME="${OS_NAME:-AtriOS}"
    RELEASE_NAME="${RELEASE:-${RELEASE_NAME:-Nebula}}"
    RELEASE_VERSION="${release_version_override:-${RELEASE_VERSION:-1.1.2}}"

    if [ ! -f "$conf_file" ]; then
        exit_with_error "Board configuration not found: $conf_file"
    fi

    # 2. Load system profile defaults BEFORE board overrides (board always wins,
    #    except an explicit caller ATRIOS_PROFILE override is preserved)
    local profile_override="${ATRIOS_PROFILE:-}"
    local requested_profile="$profile_override"
    if [ -z "$requested_profile" ]; then
        requested_profile="$(grep -E '^ATRIOS_PROFILE=' "$conf_file" | tail -n1 | cut -d'=' -f2- | sed 's/#.*//' | tr -d '\"'\''[:space:]' || true)"
    fi
    requested_profile="${requested_profile:-full}"
    local profile_conf="$PROJECT_ROOT/config/profiles/${requested_profile}.conf"
    if [ -f "$profile_conf" ]; then
        # shellcheck source=/dev/null
        source "$profile_conf"
    elif [ -d "$PROJECT_ROOT/config/profiles" ] && [ "$requested_profile" != "full" ]; then
        exit_with_error "System profile not found: $profile_conf"
    fi

    # 3. Source board configuration (overrides profile defaults)
    # shellcheck source=/dev/null
    source "$conf_file"
    if [ -n "$profile_override" ]; then
        ATRIOS_PROFILE="$profile_override"
    fi
    [ "$BOARD" = "$target_board" ] || \
        exit_with_error "Board profile identity mismatch: requested $target_board, configured $BOARD"

    # 4. Source family configuration if defined
    if [ -n "${BOARDFAMILY:-}" ]; then
        local family_conf="$PROJECT_ROOT/config/families/${BOARDFAMILY}.conf"
        if [ -f "$family_conf" ]; then
            # shellcheck source=/dev/null
            source "$family_conf"
        fi
    fi

    # Set default values if not defined by profile or board
    ATRIOS_PROFILE="${ATRIOS_PROFILE:-$requested_profile}"
    ROOTFS_DISTRO="${ROOTFS_DISTRO:-debian}"
    ATRIOS_APP_FLAVOR="${ATRIOS_APP_FLAVOR:-full}"
    ARCH="${ARCH:-arm64}"
    CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
    IMAGE_FORMAT="${IMAGE_FORMAT:-emmc}"
    IMAGE_FILENAME="${OS_NAME}-${RELEASE_NAME}-${BOARD}-emmc.img"
    BOOTLOADER_BIN="${BOOTLOADER_BIN:-${UPGRADE_DIR:?UPGRADE_DIR must be configured}/u-boot.bin.sd.bin}"
    KERNEL_IMAGE="${KERNEL_IMAGE:-output/images/Image}"
    DTB_IMAGE="${DTB_IMAGE:-output/images/${DTS_NAME}.dtb}"
    ROOTFS_IMAGE="${ROOTFS_IMAGE:-output/images/rootfs-${ROOTFS_DISTRO}-micro.ext4}"

    [[ "$BOARD" =~ ^[a-z0-9][a-z0-9-]*$ ]] || exit_with_error "Invalid board identifier: $BOARD"
    [[ "$DTS_NAME" =~ ^[a-zA-Z0-9._-]+$ ]] || exit_with_error "Invalid DTB identifier: $DTS_NAME"
    local path_var
    for path_var in BOOTLOADER_BIN KERNEL_IMAGE DTB_IMAGE ROOTFS_IMAGE; do
        if [[ "${!path_var}" != /* ]]; then
            printf -v "$path_var" '%s/%s' "$PROJECT_ROOT" "${!path_var}"
        fi
    done
    export BOARD ARCH CROSS_COMPILE RELEASE_VERSION ATRIOS_PROFILE ROOTFS_DISTRO ATRIOS_APP_FLAVOR

    display_alert "INFO" "OS / Release:" "${OS_NAME} ${RELEASE_NAME} (v${RELEASE_VERSION})"
    display_alert "INFO" "Target board:" "${BOARD_NAME} (${BOARD})"
    display_alert "INFO" "Profile / OS:" "${ATRIOS_PROFILE} (${ROOTFS_DISTRO}, app=${ATRIOS_APP_FLAVOR})"
    display_alert "INFO" "SoC & Family:" "${SOC_NAME:-$SOC} / ${BOARDFAMILY}"
    display_alert "INFO" "Target image:" "eMMC (${IMAGE_FILENAME})"
}

list_available_boards() {
    echo -e "${BOLD}Available target boards in AtriOS:${NC}"
    echo "--------------------------------------------------------------------------------------------"
    printf "%-16s | %-22s | %-8s | %-7s | %-7s | %-16s\n" "BOARD" "NAME" "PROFILE" "DISTRO" "APP" "DSP"
    echo "--------------------------------------------------------------------------------------------"
    for f in "$PROJECT_ROOT/config/boards"/*.conf; do
        [ -f "$f" ] || continue
        [ "$(basename "$f")" = "template.conf" ] && continue
        b_id="$(basename "$f" .conf)"
        (
            source "$PROJECT_ROOT/config/profiles/full.conf" 2>/dev/null || true
            b_prof="$(grep -E '^ATRIOS_PROFILE=' "$f" | tail -n1 | cut -d'=' -f2- | sed 's/#.*//' | tr -d '\"'\''[:space:]' || true)"
            [ -n "$b_prof" ] && [ -f "$PROJECT_ROOT/config/profiles/${b_prof}.conf" ] && source "$PROJECT_ROOT/config/profiles/${b_prof}.conf"
            source "$f"
            printf "%-16s | %-22s | %-8s | %-7s | %-7s | %-16s\n" \
                "$b_id" "${BOARD_NAME:-$b_id}" "${ATRIOS_PROFILE:-full}" "${ROOTFS_DISTRO:-debian}" \
                "${ATRIOS_APP_FLAVOR:-full}" "${AUDIO_AEC_ENGINE:-pbfdaf}+${AUDIO_NS_ENGINE:-gtcrn}"
        )
    done
    echo "--------------------------------------------------------------------------------------------"
}
