#!/bin/bash
# Build a selected board's reviewed U-Boot/DTS/FIP recipe.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REQUESTED_BOARD="${BOARD:-yndxsttion53}"
source "$PROJECT_ROOT/lib/general.sh"
source "$PROJECT_ROOT/lib/config.sh"
load_board_config "$REQUESTED_BOARD"
[ "$BOARD" = "$REQUESTED_BOARD" ] || exit_with_error "Board ID does not match its configuration filename"

# Hardware-specific paths live in a shared board recipe, not in this builder.
BOOTLOADER_PROFILE="${BOOTLOADER_PROFILE:-$DTS_NAME}"
[[ "$BOOTLOADER_PROFILE" =~ ^[a-zA-Z0-9._-]+$ ]] || exit_with_error "Invalid bootloader profile"
PROFILE_FILE="$PROJECT_ROOT/config/bootloaders/$BOOTLOADER_PROFILE.conf"
[ -f "$PROFILE_FILE" ] || exit_with_error "No reviewed bootloader profile for $BOARD: $BOOTLOADER_PROFILE"
source "$PROFILE_FILE"

project_source() {
    local path
    path=$(realpath -e "$PROJECT_ROOT/$1") || exit_with_error "Missing bootloader input: $1" >&2
    case "$path" in "$PROJECT_ROOT"/*) ;; *) exit_with_error "Bootloader input escapes project: $1" >&2 ;; esac
    printf '%s\n' "$path"
}

relative_target() {
    case "$1" in ''|/*|*\\*|../*|*/../*|*/..|..|./*|*/./*)
        exit_with_error "Invalid U-Boot staging path: $1" >&2 ;;
    esac
    printf '%s\n' "$1"
}

UBOOT_CONFIG_SOURCE=$(project_source "${UBOOT_CONFIG_SOURCE:?Boot profile requires UBOOT_CONFIG_SOURCE}")
UBOOT_DTS_SOURCE=$(project_source "${UBOOT_DTS_SOURCE:?Boot profile requires UBOOT_DTS_SOURCE}")
UBOOT_DTS_TARGET=$(relative_target "${UBOOT_DTS_TARGET:?Boot profile requires UBOOT_DTS_TARGET}")
[ -f "$UBOOT_CONFIG_SOURCE" ] && [ -f "$UBOOT_DTS_SOURCE" ] || exit_with_error "Bootloader config/DTS must be regular files"
if [ -n "${UBOOT_DTS_OVERLAY_SOURCE:-}${UBOOT_DTS_OVERLAY_TARGET:-}" ]; then
    UBOOT_DTS_OVERLAY_SOURCE=$(project_source "${UBOOT_DTS_OVERLAY_SOURCE:?Missing overlay source}")
    UBOOT_DTS_OVERLAY_TARGET=$(relative_target "${UBOOT_DTS_OVERLAY_TARGET:?Missing overlay target}")
    [ -f "$UBOOT_DTS_OVERLAY_SOURCE" ] || exit_with_error "Bootloader overlay must be a regular file"
fi
if [ -n "${UBOOT_AML_BURN_SOURCE:-}" ]; then
    UBOOT_AML_BURN_SOURCE=$(project_source "$UBOOT_AML_BURN_SOURCE")
    [ -f "$UBOOT_AML_BURN_SOURCE/install.sh" ] || exit_with_error "Missing AML gadget installer"
fi
FIP_PROFILE="${FIP_PROFILE:-${FIP_SOC:-}}"
[[ "$FIP_PROFILE" =~ ^[a-zA-Z0-9_-]+$ ]] || exit_with_error "Boot profile requires a valid FIP_PROFILE"
[[ "${UPGRADE_DIR:-}" != /* ]] || exit_with_error "UPGRADE_DIR must be relative to project"
OUTPUT_DIR=$(realpath -m "$PROJECT_ROOT/${UPGRADE_DIR:?Board requires UPGRADE_DIR}")
case "$OUTPUT_DIR" in "$PROJECT_ROOT"/*) ;; *) exit_with_error "UPGRADE_DIR escapes project" ;; esac
[ "$(realpath -m "$BOOTLOADER_BIN")" = "$OUTPUT_DIR/u-boot.bin.sd.bin" ] || \
    exit_with_error "BOOTLOADER_BIN must reference the selected UPGRADE_DIR/u-boot.bin.sd.bin"

UBOOT_SRC="${UBOOT_SRC:-$PROJECT_ROOT/cache/uboot}"
FIP_TOOL_DIR="${FIP_TOOL_DIR:-$PROJECT_ROOT/cache/fip}"
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
export ARCH="${UBOOT_ARCH:-arm}" CROSS_COMPILE

echo "Building U-Boot for $BOARD using $BOOTLOADER_PROFILE / FIP $FIP_PROFILE"
if [ ! -d "$UBOOT_SRC" ]; then
    if [ -e "$UBOOT_SRC" ] || [ -L "$UBOOT_SRC" ]; then
        exit_with_error "Refusing to replace existing UBOOT_SRC path"
    fi
    mkdir -p "$(dirname "$UBOOT_SRC")"
    if [ -d "$PROJECT_ROOT/u-boot-mainline" ]; then
        ln -s "$PROJECT_ROOT/u-boot-mainline" "$UBOOT_SRC"
    else
        git clone --depth 1 -b "${UBOOT_GIT_REF:-master}" \
            "${UBOOT_GIT_URL:-https://github.com/leftymods/u-boot.git}" "$UBOOT_SRC"
    fi
fi
[ -f "$UBOOT_SRC/Makefile" ] || exit_with_error "Invalid UBOOT_SRC tree"
if [ ! -d "$FIP_TOOL_DIR/$FIP_PROFILE" ]; then
    if [ -e "$FIP_TOOL_DIR" ] || [ -L "$FIP_TOOL_DIR" ]; then
        exit_with_error "FIP tree does not contain selected profile: $FIP_PROFILE"
    fi
    mkdir -p "$(dirname "$FIP_TOOL_DIR")"
    git clone --depth 1 https://github.com/LibreELEC/amlogic-boot-fip.git "$FIP_TOOL_DIR"
fi
[ -d "$FIP_TOOL_DIR/$FIP_PROFILE" ] && [ -f "$FIP_TOOL_DIR/build-fip.sh" ] || \
    exit_with_error "Invalid FIP tool/profile tree"

stage_source() {
    local destination base
    base=$(realpath -e "$UBOOT_SRC")
    destination=$(realpath -m "$UBOOT_SRC/$2")
    case "$destination" in "$base"/*) ;; *) exit_with_error "U-Boot staging destination escapes tree" ;; esac
    [ ! -L "$UBOOT_SRC/$2" ] || exit_with_error "Refusing U-Boot staging symlink"
    mkdir -p "$(dirname "$destination")"
    cp "$1" "$destination"
}
stage_source "$UBOOT_DTS_SOURCE" "$UBOOT_DTS_TARGET"
if [ -n "${UBOOT_DTS_OVERLAY_SOURCE:-}" ]; then
    stage_source "$UBOOT_DTS_OVERLAY_SOURCE" "$UBOOT_DTS_OVERLAY_TARGET"
fi
if [ -n "${UBOOT_AML_BURN_SOURCE:-}" ]; then
    bash "$UBOOT_AML_BURN_SOURCE/install.sh" "$UBOOT_SRC"
fi
BUILD_JOBS="${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"
[[ "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]] || exit_with_error "Invalid BUILD_JOBS"
BUILD_CONFIG="atrios_${BOARD//-/_}_defconfig"
stage_source "$UBOOT_CONFIG_SOURCE" "configs/$BUILD_CONFIG"
(
    cd "$UBOOT_SRC"
    make "$BUILD_CONFIG"
    for option in ${UBOOT_REQUIRED_OPTIONS:-}; do
        [[ "$option" =~ ^CONFIG_[A-Z0-9_]+$ ]] || exit_with_error "Invalid required U-Boot option: $option"
        grep -qx "${option}=y" .config || exit_with_error "Required U-Boot option missing: $option"
    done
    make -j"$BUILD_JOBS"
)
MAINLINE_UBOOT_BIN="$UBOOT_SRC/u-boot.bin"
[ -s "$MAINLINE_UBOOT_BIN" ] || exit_with_error "U-Boot did not generate u-boot.bin"
mkdir -p "$OUTPUT_DIR"
(
    cd "$FIP_TOOL_DIR"
    bash ./build-fip.sh "$FIP_PROFILE" "$MAINLINE_UBOOT_BIN" "$OUTPUT_DIR"
)
for artifact in u-boot.bin u-boot.bin.sd.bin u-boot.bin.usb.bl2 u-boot.bin.usb.tpl; do
    [ -s "$OUTPUT_DIR/$artifact" ] || exit_with_error "FIP did not generate $artifact for $BOARD"
done
cp "$OUTPUT_DIR/u-boot.bin" "$OUTPUT_DIR/bootloader.img"
echo "SUCCESS: $BOARD bootloader assets in $OUTPUT_DIR"
