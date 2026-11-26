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

# A branch/tag name is not a source pin. Existing local trees are verified,
# never reset or cleaned; each new pin gets its own cache by default.
for pin in UBOOT_GIT_COMMIT FIP_GIT_COMMIT; do
    [[ "${!pin:-}" =~ ^[0-9a-f]{40}$ ]] || exit_with_error "Boot recipe requires immutable $pin (40 lowercase hex characters)"
done
: "${UBOOT_GIT_URL:?Boot recipe requires UBOOT_GIT_URL}"
: "${FIP_GIT_URL:?Boot recipe requires FIP_GIT_URL}"

select_source_cache() {
    local explicit="$1" legacy="$2" kind="$3" commit="$4"
    if [ -n "$explicit" ]; then
        printf '%s\n' "$explicit"
    elif [ -d "$legacy" ] && [ "$(git -C "$legacy" rev-parse HEAD 2>/dev/null)" = "$commit" ]; then
        # Reuse a previously validated tree without moving users' local files.
        printf '%s\n' "$legacy"
    else
        printf '%s/cache/sources/%s-%s\n' "$PROJECT_ROOT" "$kind" "$commit"
    fi
}

verify_source_pin() {
    local directory="$1" commit="$2" label="$3" actual top
    [ -d "$directory" ] || exit_with_error "$label source directory is unavailable"
    top=$(git -C "$directory" rev-parse --show-toplevel 2>/dev/null) || \
        exit_with_error "$label source is not a Git worktree; existing files were preserved"
    [ "$(realpath -e "$top")" = "$(realpath -e "$directory")" ] || \
        exit_with_error "$label source must be the Git worktree root"
    actual=$(git -C "$directory" rev-parse --verify HEAD) || exit_with_error "$label source has no valid HEAD"
    [ "$actual" = "$commit" ] || \
        exit_with_error "$label source pin mismatch: expected $commit, found $actual; existing files were preserved"
}

prepare_git_source() {
    local directory="$1" url="$2" commit="$3" label="$4" parent temporary
    if [ -e "$directory" ] || [ -L "$directory" ]; then
        verify_source_pin "$directory" "$commit" "$label"
        return
    fi
    parent=$(dirname "$directory")
    mkdir -p "$parent"
    temporary=$(mktemp -d "$parent/.atrios-git-fetch.XXXXXXXX")
    if ! (
        git -C "$temporary" init -q &&
        git -C "$temporary" remote add origin "$url" &&
        git -C "$temporary" fetch --depth=1 origin "$commit" &&
        git -C "$temporary" -c advice.detachedHead=false checkout --detach FETCH_HEAD &&
        verify_source_pin "$temporary" "$commit" "$label"
    ); then
        rm -rf -- "$temporary"
        exit_with_error "Failed to fetch the pinned $label source"
    fi
    # Do not replace a tree created by another builder while fetch was running.
    if ! mv -T --no-clobber "$temporary" "$directory" || [ -e "$temporary" ]; then
        rm -rf -- "$temporary"
        exit_with_error "$label source path appeared concurrently; existing files were preserved"
    fi
    verify_source_pin "$directory" "$commit" "$label"
}

UBOOT_SRC=$(select_source_cache "${UBOOT_SRC:-}" "$PROJECT_ROOT/cache/uboot" uboot "$UBOOT_GIT_COMMIT")
FIP_TOOL_DIR=$(select_source_cache "${FIP_TOOL_DIR:-}" "$PROJECT_ROOT/cache/fip" fip "$FIP_GIT_COMMIT")
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
export ARCH="${UBOOT_ARCH:-arm}" CROSS_COMPILE

echo "Building U-Boot for $BOARD using $BOOTLOADER_PROFILE / FIP $FIP_PROFILE"
prepare_git_source "$UBOOT_SRC" "$UBOOT_GIT_URL" "$UBOOT_GIT_COMMIT" U-Boot
prepare_git_source "$FIP_TOOL_DIR" "$FIP_GIT_URL" "$FIP_GIT_COMMIT" FIP
[ -f "$UBOOT_SRC/Makefile" ] || exit_with_error "Invalid UBOOT_SRC tree"
[ -d "$FIP_TOOL_DIR/$FIP_PROFILE" ] && [ -f "$FIP_TOOL_DIR/build-fip.sh" ] || \
    exit_with_error "Invalid FIP tool/profile tree"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$UBOOT_SRC" show -s --format=%ct HEAD)}"
[[ "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]] || exit_with_error "Invalid SOURCE_DATE_EPOCH"
export SOURCE_DATE_EPOCH

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

# Record the actual validated bases and local staging delta. A locally modified
# tree remains intact and is visibly different from a clean pinned checkout.
python3 - "$UBOOT_SRC" "$FIP_TOOL_DIR" "$OUTPUT_DIR/bootloader-sources.json" \
    "$BOARD" "$BOOTLOADER_PROFILE" "$UBOOT_CONFIG_SOURCE" "$UBOOT_DTS_SOURCE" "$SOURCE_DATE_EPOCH" <<'PYPROVENANCE'
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

uboot, fip, output, board, profile, config, dts, epoch = sys.argv[1:]

def git(tree, *args):
    return subprocess.check_output(['git', '-C', tree, *args])

def sources(tree):
    untracked = {}
    for name in git(tree, 'ls-files', '--others', '--exclude-standard', '-z').split(b'\0'):
        if not name:
            continue
        relative = os.fsdecode(name)
        path = Path(tree) / relative
        if path.is_symlink():
            untracked[relative] = {'symlink': os.readlink(path)}
        elif path.is_file():
            digest = hashlib.sha256()
            with path.open('rb') as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
            untracked[relative] = {'sha256': digest.hexdigest()}
    return {'commit': git(tree, 'rev-parse', 'HEAD').decode().strip(),
            'tree': git(tree, 'rev-parse', 'HEAD^{tree}').decode().strip(),
            'tracked_diff_sha256': hashlib.sha256(git(tree, 'diff', '--binary', 'HEAD')).hexdigest(),
            'dirty': bool(git(tree, 'status', '--porcelain', '--untracked-files=normal')),
            'untracked': untracked}

value = {'schema': 1, 'board': board, 'profile': profile,
         'source_date_epoch': int(epoch), 'uboot': sources(uboot), 'fip': sources(fip),
         'config_sha256': hashlib.sha256(Path(config).read_bytes()).hexdigest(),
         'dts_sha256': hashlib.sha256(Path(dts).read_bytes()).hexdigest(),
         'resolved_config_sha256': hashlib.sha256((Path(uboot) / '.config').read_bytes()).hexdigest(),
         'artifacts': {name: hashlib.sha256((Path(output).parent / name).read_bytes()).hexdigest()
                       for name in ('u-boot.bin', 'u-boot.bin.sd.bin', 'u-boot.bin.usb.bl2', 'u-boot.bin.usb.tpl', 'bootloader.img')}}
target = Path(output)
fd, temporary = tempfile.mkstemp(prefix='.bootloader-sources-', dir=target.parent)
try:
    with os.fdopen(fd, 'w') as stream:
        json.dump(value, stream, sort_keys=True, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, target)
finally:
    if os.path.exists(temporary):
        os.unlink(temporary)
PYPROVENANCE
echo "SUCCESS: $BOARD bootloader assets and source provenance in $OUTPUT_DIR"
