#!/bin/bash
# Resolve release artifact/trust-anchor paths from the same board config as builds.
release_board_config() {
    local selected="${1:?board is required}" path root
    PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
    source "$PROJECT_ROOT/lib/general.sh"
    source "$PROJECT_ROOT/lib/config.sh"
    load_board_config "$selected"
    [ "$BOARD" = "$selected" ] || exit_with_error "Board ID does not match selected release profile"
    root=$(realpath -e "$PROJECT_ROOT")
    for path in "${ROOTFS_OVERLAY:?ROOTFS_OVERLAY is required}" "$DTB_IMAGE"; do
        [[ "$path" != *$'\n'* && "$path" != *$'\r'* ]] || exit_with_error "Invalid release path"
    done
    [[ "$ROOTFS_OVERLAY" != /* ]] || exit_with_error "ROOTFS_OVERLAY must be project-relative"
    ROOTFS_OVERLAY_PATH=$(realpath -e "$root/$ROOTFS_OVERLAY")
    DTB_IMAGE=$(realpath -m "$DTB_IMAGE")
    for path in "$ROOTFS_OVERLAY_PATH" "$DTB_IMAGE"; do
        case "$path" in "$root"/*) ;; *) exit_with_error "Release input escapes project" ;; esac
    done
    [ -d "$ROOTFS_OVERLAY_PATH" ] || exit_with_error "Release rootfs overlay is missing"
    case "$IMAGE_FILENAME" in ''|*/*|*\\*|*$'\n'*|*$'\r'*) exit_with_error "Invalid release image filename" ;; esac
    OTA_RUNTIME_MANIFEST_PATH=""
    if [ -n "${OTA_RUNTIME_MANIFEST:-}" ]; then
        case "$OTA_RUNTIME_MANIFEST" in /*|*\\*|*$'\n'*|*$'\r'*)
            exit_with_error "OTA_RUNTIME_MANIFEST must be project-relative" ;; esac
        OTA_RUNTIME_MANIFEST_PATH=$(realpath -e "$root/$OTA_RUNTIME_MANIFEST")
        case "$OTA_RUNTIME_MANIFEST_PATH" in "$root"/*) ;; *)
            exit_with_error "Runtime manifest escapes project" ;; esac
        [ -f "$OTA_RUNTIME_MANIFEST_PATH" ] || exit_with_error "Runtime manifest is missing"
    fi
    export ROOTFS_OVERLAY_PATH DTB_IMAGE IMAGE_FILENAME OTA_RUNTIME_MANIFEST_PATH
}
