#!/bin/bash
# ==============================================================================
# AtriOS Build System - Configuration Loader
# ==============================================================================

load_board_config() {
    local target_board="${1:-$BOARD}"
    local conf_file="$PROJECT_ROOT/config/boards/${target_board}.conf"
    local system_conf="$PROJECT_ROOT/config/system.conf"

    # 1. Load System & Release Configuration
    if [ -f "$system_conf" ]; then
        # shellcheck source=/dev/null
        source "$system_conf"
    fi

    # Allow CLI / environment override of release
    OS_NAME="${OS_NAME:-AtriOS}"
    RELEASE_NAME="${RELEASE:-${RELEASE_NAME:-Nebula}}"
    RELEASE_VERSION="${RELEASE_VERSION:-1.0}"

    if [ ! -f "$conf_file" ]; then
        exit_with_error "Board configuration not found: $conf_file"
    fi

    # 2. Source board configuration
    # shellcheck source=/dev/null
    source "$conf_file"

    # 3. Source family configuration if defined
    if [ -n "${BOARDFAMILY:-}" ]; then
        local family_conf="$PROJECT_ROOT/config/families/${BOARDFAMILY}.conf"
        if [ -f "$family_conf" ]; then
            # shellcheck source=/dev/null
            source "$family_conf"
        fi
    fi

    # Set default values if not defined by board
    ARCH="${ARCH:-arm64}"
    CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
    IMAGE_FORMAT="${IMAGE_FORMAT:-emmc}"
    IMAGE_FILENAME="${OS_NAME}-${RELEASE_NAME}-${BOARD}-emmc.img"
    BOOTLOADER_BIN="${BOOTLOADER_BIN:-device/yandex/station_max/upgrade/u-boot.bin.sd.bin}"
    KERNEL_IMAGE="${KERNEL_IMAGE:-/home/lmods/linux-7.3.1/arch/arm64/boot/Image}"
    DTB_IMAGE="${DTB_IMAGE:-output/images/meson-sm1-yandex-station-max.dtb}"
    ROOTFS_IMAGE="${ROOTFS_IMAGE:-buildroot/output/images/rootfs.ext4}"

    display_alert "INFO" "OS / Release:" "${OS_NAME} ${RELEASE_NAME} (v${RELEASE_VERSION})"
    display_alert "INFO" "Target board:" "${BOARD_NAME} (${BOARD})"
    display_alert "INFO" "SoC & Family:" "${SOC_NAME:-$SOC} / ${BOARDFAMILY}"
    display_alert "INFO" "Target image:" "eMMC (${IMAGE_FILENAME})"
}

list_available_boards() {
    echo -e "${BOLD}Available target boards in AtriOS:${NC}"
    echo "----------------------------------------------------------------------"
    printf "%-18s | %-24s | %-12s | %-10s\n" "BOARD" "NAME" "SOC" "ARCH"
    echo "----------------------------------------------------------------------"
    for f in "$PROJECT_ROOT/config/boards"/*.conf; do
        [ -f "$f" ] || continue
        [ "$(basename "$f")" = "template.conf" ] && continue
        b_id="$(basename "$f" .conf)"
        b_name="$(grep -E '^BOARD_NAME=' "$f" | cut -d'=' -f2- | tr -d '\"')"
        b_soc="$(grep -E '^SOC_NAME=' "$f" | cut -d'=' -f2- | tr -d '\"')"
        b_arch="$(grep -E '^ARCH=' "$f" | cut -d'=' -f2- | tr -d '\"')"
        printf "%-18s | %-24s | %-12s | %-10s\n" "$b_id" "$b_name" "$b_soc" "$b_arch"
    done
    echo "----------------------------------------------------------------------"
}
