#!/bin/bash
# ==============================================================================
# AtriOS Build System - Main Entry Point
# ==============================================================================
# Usage:
#   ./compile.sh                                 # Default build (station-max, Nebula)
#   ./compile.sh BOARD=station-max               # Build eMMC image
#   ./compile.sh BOARD=station-max RELEASE=Nebula # Specify custom release name
#   ./compile.sh TARGET=kernel                   # Compile Linux kernel
#   ./compile.sh TARGET=uboot                    # Build bootloader
#   ./compile.sh list                            # Show supported boards
#   ./compile.sh info station-max                # Show board details
#   ./compile.sh clean                           # Clean build artifacts
# ==============================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PROJECT_ROOT

# Source library modules
source "$PROJECT_ROOT/lib/general.sh"
source "$PROJECT_ROOT/lib/config.sh"
source "$PROJECT_ROOT/lib/compilation-kernel.sh"
source "$PROJECT_ROOT/lib/image-emmc.sh"

# Default parameters
BOARD="station-max"
TARGET="image"
RELEASE=""

# Parse CLI arguments
for arg in "$@"; do
    case "$arg" in
        BOARD=*)
            BOARD="${arg#*=}"
            ;;
        TARGET=*)
            TARGET="${arg#*=}"
            ;;
        RELEASE=*|RELEASE_NAME=*)
            RELEASE="${arg#*=}"
            export RELEASE
            ;;
        kernel)
            TARGET="kernel"
            ;;
        uboot)
            TARGET="uboot"
            ;;
        image|emmc)
            TARGET="image"
            ;;
        list|boards)
            list_available_boards
            exit 0
            ;;
        info)
            TARGET="info"
            ;;
        clean)
            TARGET="clean"
            ;;
        help|--help|-h)
            cat <<EOF
AtriOS Build System

Usage:
  ./compile.sh [BOARD=<name>] [RELEASE=<release>] [TARGET=<target>]
  ./compile.sh [command]

Targets:
  image     Build full bootable eMMC image for Balena Etcher / dd (default)
  kernel    Compile Linux kernel (Image, DTB, modules)
  uboot     Build bootloader (U-Boot & FIP)
  rootfs    Build root filesystem
  info      Display board and release configuration
  clean     Clean build output directory

Commands:
  list      List all supported target boards
  kernel    Compile Linux kernel directly
  uboot     Compile U-Boot directly
  help      Display this help message

Examples:
  ./compile.sh
  ./compile.sh kernel
  ./compile.sh BOARD=station-max RELEASE=Nebula
  ./compile.sh TARGET=kernel
  ./compile.sh TARGET=uboot
  ./compile.sh list
  ./compile.sh info
EOF
            exit 0
            ;;
        *)
            if [ -f "$PROJECT_ROOT/config/boards/${arg}.conf" ]; then
                BOARD="$arg"
            fi
            ;;
    esac
done

# Load configuration for selected board & release
load_board_config "$BOARD"

# Execute requested target
case "$TARGET" in
    image|emmc)
        build_emmc_image
        ;;
    kernel)
        build_kernel
        ;;
    uboot)
        display_alert "INFO" "Building U-Boot for" "$BOARD"
        bash "$PROJECT_ROOT/scripts/build_mainline_uboot.sh"
        ;;
    info)
        echo "=============================================================================="
        echo "OS Name:         $OS_NAME"
        echo "Release:         $RELEASE_NAME (v$RELEASE_VERSION)"
        echo "Board ID:        $BOARD"
        echo "Board Name:      $BOARD_NAME"
        echo "Vendor:          $VENDOR"
        echo "SoC:             ${SOC_NAME:-$SOC}"
        echo "Arch:            $ARCH"
        echo "DTS:             $DTS_NAME"
        echo "U-Boot Config:   $UBOOT_DEFCONFIG"
        echo "Kernel Config:   $KERNEL_DEFCONFIG"
        echo "Bootloader:      $BOOTLOADER_BIN"
        echo "Console:         $SERIAL_CONSOLE"
        echo "Image Format:    $IMAGE_FORMAT"
        echo "Output Image:    $IMAGE_FILENAME"
        echo "=============================================================================="
        ;;
    clean)
        display_alert "INFO" "Cleaning output directory..."
        rm -rf "$PROJECT_ROOT/output/images"/*.img
        display_alert "OK" "Output directory cleaned."
        ;;
    *)
        exit_with_error "Unknown target: $TARGET" "Supported: image, kernel, uboot, info, clean"
        ;;
esac
