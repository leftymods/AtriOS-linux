#!/bin/bash
# ==============================================================================
# AtriOS Build System - Main Entry Point
# ==============================================================================
# Usage:
#   ./compile.sh                                 # Default build (yndxsttion53, Nebula)
#   ./compile.sh BOARD=yndxsttion53               # Build eMMC image
#   ./compile.sh BOARD=yndxsttion53 RELEASE=Nebula # Specify custom release name
#   ./compile.sh TARGET=kernel                   # Compile Linux kernel
#   ./compile.sh TARGET=packages                 # Build .deb packages for U-Boot & Kernel
#   ./compile.sh TARGET=rootfs                   # Build super micro Debian rootfs
#   ./compile.sh TARGET=uboot                    # Build bootloader
#   ./compile.sh list                            # Show supported boards
#   ./compile.sh info yndxsttion53                # Show board details
#   ./compile.sh clean                           # Clean build artifacts
# ==============================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PROJECT_ROOT

# Source library modules
source "$PROJECT_ROOT/lib/general.sh"
source "$PROJECT_ROOT/lib/config.sh"
source "$PROJECT_ROOT/lib/compilation-kernel.sh"
source "$PROJECT_ROOT/lib/packages.sh"
source "$PROJECT_ROOT/lib/rootfs-debian.sh"
source "$PROJECT_ROOT/lib/image-emmc.sh"

# Default parameters
BOARD="yndxsttion53"
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
        packages|debs)
            TARGET="packages"
            ;;
        rootfs)
            TARGET="rootfs"
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
  image     Build full bootable eMMC image (packages -> rootfs -> image)
  packages  Build .deb packages for U-Boot and Kernel
  rootfs    Build Super Micro Debian arm64 rootfs (ext4)
  kernel    Compile Linux kernel (Image, DTB, modules)
  uboot     Build bootloader (U-Boot & FIP)
  info      Display board and release configuration
  clean     Clean build output directory

Commands:
  list      List all supported target boards
  packages  Build .deb packages
  rootfs    Build root filesystem
  kernel    Compile Linux kernel directly
  uboot     Compile U-Boot directly
  help      Display this help message

Examples:
  ./compile.sh
  ./compile.sh TARGET=packages
  ./compile.sh TARGET=rootfs
  ./compile.sh BOARD=yndxsttion53 RELEASE=Nebula
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
        # Ensure packages and rootfs are prepared
        display_alert "INFO" "Checking required components for image..."
        if [ ! -f "$PROJECT_ROOT/output/packages/atrios-u-boot-${BOARD}_1.0_arm64.deb" ] || \
           [ ! -f "$PROJECT_ROOT/output/packages/atrios-linux-image-7.3.1_7.3.1-1_arm64.deb" ]; then
            display_alert "INFO" "Packages missing, building .deb packages first..."
            build_all_packages
        fi

        if [ ! -f "$PROJECT_ROOT/$ROOTFS_IMAGE" ]; then
            display_alert "INFO" "RootFS missing ($ROOTFS_IMAGE), building Super Micro Debian RootFS..."
            build_debian_micro_rootfs
        fi

        build_emmc_image
        ;;
    packages|debs)
        build_all_packages
        ;;
    rootfs)
        build_debian_micro_rootfs
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
        echo "RootFS Target:   $ROOTFS_IMAGE"
        echo "Image Format:    $IMAGE_FORMAT"
        echo "Output Image:    $IMAGE_FILENAME"
        echo "=============================================================================="
        ;;
    clean)
        display_alert "INFO" "Cleaning output directory..."
        rm -rf "$PROJECT_ROOT/output/images"/*.img "$PROJECT_ROOT/output/packages"/*.deb
        display_alert "OK" "Output directory cleaned."
        ;;
    *)
        exit_with_error "Unknown target: $TARGET" "Supported: image, packages, rootfs, kernel, uboot, info, clean"
        ;;
esac
