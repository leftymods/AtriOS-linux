#!/bin/bash
# ==============================================================================
# AtriOS Build System - Modular RootFS Dispatcher
# ==============================================================================
# Dispatches rootfs generation based on the board/profile ROOTFS_DISTRO:
#   - debian : Super Micro Debian Bookworm (full profile, flagship speakers)
#   - alpine : Alpine Linux musl/BusyBox (lite / minimal profiles, older/low-RAM speakers)

build_rootfs() {
    local distro="${ROOTFS_DISTRO:-debian}"
    case "$distro" in
        debian)
            build_debian_micro_rootfs
            ;;
        alpine)
            build_alpine_micro_rootfs
            ;;
        *)
            exit_with_error "Unsupported ROOTFS_DISTRO: $distro (expected debian or alpine)"
            ;;
    esac
}
