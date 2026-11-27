#!/bin/bash
# ==============================================================================
# AtriOS Unified Modular Builder (Runtime + Audio/DSP + Optional Decoders)
# ==============================================================================
# Builds ONLY the modules enabled by a board's profile (config/profiles/*.conf)
# and board config (config/boards/<board>.conf):
#   - atrios-runtime (flavor: full | lite | minimal, with disabled applets gated)
#   - atri-aec       (via scripts/build_audio_dsp.sh)
#   - atri-mp3dec    (only when MOD_YMUSIC=true)
#   - Prunes disabled init.d / lib / ONNX artifacts and writes /etc/atrios/modules.json
#
# Usage:
#   scripts/build_atrios_modules.sh --board <name> --stage-dir <dir> [--native] [--skip-runtime]

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PROJECT_ROOT

source "$PROJECT_ROOT/lib/general.sh"
source "$PROJECT_ROOT/lib/config.sh"
source "$PROJECT_ROOT/lib/modules.sh"

BOARD_NAME_ARG=""
STAGE_DIR=""
NATIVE_BUILD=0
SKIP_RUNTIME=0

while [ $# -gt 0 ]; do
    case "$1" in
        --board)        BOARD_NAME_ARG="$2"; shift 2 ;;
        --stage-dir)    STAGE_DIR="$2"; shift 2 ;;
        --native)       NATIVE_BUILD=1; shift ;;
        --skip-runtime) SKIP_RUNTIME=1; shift ;;
        -h|--help)
            sed -n '2,15p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 2
            ;;
    esac
done

[ -n "$BOARD_NAME_ARG" ] || { echo "Error: --board <name> is required" >&2; exit 2; }
[ -n "$STAGE_DIR" ] || { echo "Error: --stage-dir <dir> is required" >&2; exit 2; }

load_board_config "$BOARD_NAME_ARG"

mkdir -p "$STAGE_DIR/usr/lib/atrios" "$STAGE_DIR/usr/bin" "$STAGE_DIR/etc/atrios"

SRC_DIR="$PROJECT_ROOT/board/yandex/station2/src"
CC_BIN="${CC:-${CROSS_COMPILE:-aarch64-linux-gnu-}gcc}"
if [ "$NATIVE_BUILD" -eq 1 ]; then
    CC_BIN="gcc"
fi

# 1. Build modular Audio/DSP (atri-aec + optional GTCRN/ONNX)
bash "$PROJECT_ROOT/scripts/build_audio_dsp.sh" \
    --board "$BOARD" \
    --cc "$CC_BIN" \
    --output "$STAGE_DIR/usr/lib/atrios/atri-aec" \
    --stage-dir "$STAGE_DIR"

# 2. Build atri-mp3dec ONLY when MOD_YMUSIC=true
if [ "${MOD_YMUSIC:-true}" = "true" ]; then
    "$CC_BIN" -O2 "$SRC_DIR/atri_mp3dec.c" -lm -o "$STAGE_DIR/usr/bin/atri-mp3dec"
    chmod 755 "$STAGE_DIR/usr/bin/atri-mp3dec"
    echo "[modules] built atri-mp3dec ($(stat -c %s "$STAGE_DIR/usr/bin/atri-mp3dec") bytes)"
else
    echo "[modules] skipped atri-mp3dec (MOD_YMUSIC=${MOD_YMUSIC})"
fi

# 3. Build modular atrios-runtime with flavor + applet cfg flags
export PATH="$HOME/.cargo/bin:$PATH"
if [ "$SKIP_RUNTIME" -eq 0 ] && command -v rustc >/dev/null 2>&1; then
    RUST_FLAGS=(--edition=2021)
    case "${ATRIOS_APP_FLAVOR:-full}" in
        lite)    RUST_FLAGS+=(--cfg atrios_flavor_lite -C opt-level=z -C strip=symbols) ;;
        minimal) RUST_FLAGS+=(--cfg atrios_flavor_minimal -C opt-level=z -C strip=symbols) ;;
        full)    RUST_FLAGS+=(-C opt-level=2) ;;
        *)       exit_with_error "Unsupported ATRIOS_APP_FLAVOR: ${ATRIOS_APP_FLAVOR}" ;;
    esac

    [ "${MOD_DISPLAY:-true}" = "false" ]   && RUST_FLAGS+=(--cfg atrios_no_display)
    [ "${MOD_ROTARY:-true}" = "false" ]    && RUST_FLAGS+=(--cfg atrios_no_rotary)
    [ "${MOD_LIGHT:-true}" = "false" ]     && RUST_FLAGS+=(--cfg atrios_no_light)
    [ "${MOD_BLUETOOTH:-true}" = "false" ] && RUST_FLAGS+=(--cfg atrios_no_bluetooth)
    [ "${MOD_OTA:-true}" = "false" ]       && RUST_FLAGS+=(--cfg atrios_no_ota)
    [ "${MOD_YMUSIC:-true}" = "false" ]    && RUST_FLAGS+=(--cfg atrios_no_ymusic)
    [ "${MOD_ATRIAI:-true}" = "false" ]    && RUST_FLAGS+=(--cfg atrios_no_atriai)

    if [ "$NATIVE_BUILD" -eq 0 ]; then
        RUST_TARGET="${RUST_TARGET:-aarch64-unknown-linux-gnu}"
        RUST_FLAGS+=(--target "$RUST_TARGET" -C "linker=$CC_BIN")
    fi

    echo "[modules] building atrios-runtime flavor=${ATRIOS_APP_FLAVOR:-full}"
    rustc "${RUST_FLAGS[@]}" "$SRC_DIR/atrios_runtime.rs" -o "$STAGE_DIR/usr/lib/atrios/atrios-runtime"
    chmod 755 "$STAGE_DIR/usr/lib/atrios/atrios-runtime"
    echo "[modules] built atrios-runtime ($(stat -c %s "$STAGE_DIR/usr/lib/atrios/atrios-runtime") bytes)"
fi

# 4. Prune disabled modules and write manifests
prune_disabled_rootfs_modules "$STAGE_DIR"
write_hardware_manifest "$STAGE_DIR"
write_modules_manifest "$STAGE_DIR"
echo "[modules] staged modular system for board=$BOARD profile=${ATRIOS_PROFILE:-full} distro=${ROOTFS_DISTRO:-debian} app=${ATRIOS_APP_FLAVOR:-full}"
