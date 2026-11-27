#!/bin/bash
# Modular Audio/DSP builder for AtriOS speakers.
# Selects only the AEC/NS/VQE modules enabled by config/boards/<board>.conf
# (or environment overrides: AUDIO_AEC_ENGINE, AUDIO_NS_ENGINE, AUDIO_VQE_MICS,
# AUDIO_ENABLE_STFT_KALMAN, HW_CAPTURE_DEVICE, HW_MIC_CHANNELS).
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD=""
OUTPUT=""
STAGE_DIR=""
CC_BIN="${CC:-aarch64-linux-gnu-gcc}"

while [ $# -gt 0 ]; do
    case "$1" in
        --board)     BOARD="$2"; shift 2 ;;
        --output)    OUTPUT="$2"; shift 2 ;;
        --stage-dir) STAGE_DIR="$2"; shift 2 ;;
        --cc)        CC_BIN="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 --board <name> [--output <atri-aec-path>] [--stage-dir <rootfs-stage>] [--cc <compiler>]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

[ -n "$BOARD" ] || { echo "Error: --board is required" >&2; exit 2; }
CONF="$PROJECT_ROOT/config/boards/${BOARD}.conf"
[ -f "$CONF" ] || { echo "Error: board config not found: $CONF" >&2; exit 2; }

# Preserve any caller env overrides before sourcing board profile
OVR_AEC="${AUDIO_AEC_ENGINE:-}"
OVR_NS="${AUDIO_NS_ENGINE:-}"
OVR_MICS="${AUDIO_VQE_MICS:-}"
OVR_KALMAN="${AUDIO_ENABLE_STFT_KALMAN:-}"
OVR_DEV="${HW_CAPTURE_DEVICE:-}"
OVR_CH="${HW_MIC_CHANNELS:-}"

# Load system profile defaults first if defined in board config
PROFILE_NAME="$(grep -E '^ATRIOS_PROFILE=' "$CONF" | tail -n1 | cut -d'=' -f2- | sed 's/#.*//' | tr -d '\"'\''[:space:]' || true)"
PROFILE_NAME="${PROFILE_NAME:-full}"
if [ -f "$PROJECT_ROOT/config/profiles/${PROFILE_NAME}.conf" ]; then
    # shellcheck disable=SC1090
    source "$PROJECT_ROOT/config/profiles/${PROFILE_NAME}.conf"
fi

# shellcheck disable=SC1090
source "$CONF"

AUDIO_AEC_ENGINE="${OVR_AEC:-${AUDIO_AEC_ENGINE:-pbfdaf}}"
AUDIO_NS_ENGINE="${OVR_NS:-${AUDIO_NS_ENGINE:-bnr}}"
AUDIO_VQE_MICS="${OVR_MICS:-${AUDIO_VQE_MICS:-${HW_MIC_CHANNELS:-7}}}"
AUDIO_ENABLE_STFT_KALMAN="${OVR_KALMAN:-${AUDIO_ENABLE_STFT_KALMAN:-true}}"
HW_CAPTURE_DEVICE="${OVR_DEV:-${HW_CAPTURE_DEVICE:-hw:0,3}}"
HW_MIC_CHANNELS="${OVR_CH:-${HW_MIC_CHANNELS:-8}}"

if [ "$AUDIO_VQE_MICS" -gt "$HW_MIC_CHANNELS" ]; then
    AUDIO_VQE_MICS="$HW_MIC_CHANNELS"
fi

SRC_DIR="$PROJECT_ROOT/board/yandex/station2/src"
OVERLAY_DIR="$PROJECT_ROOT/${ROOTFS_OVERLAY:-board/yandex/station2/rootfs_overlay}"
[ -n "$OUTPUT" ] || OUTPUT="$OVERLAY_DIR/usr/lib/atrios/atri-aec"

SRCS=("$SRC_DIR/atri_aec.c")
CFLAGS=(-O3 -Wall -Wextra
    "-DATRIOS_CAP_DEV=\"$HW_CAPTURE_DEVICE\""
    "-DATRIOS_CAP_CH=$HW_MIC_CHANNELS"
    "-DATRIOS_VQE_MICS=$AUDIO_VQE_MICS"
)
LDFLAGS=(-lm)

case "$AUDIO_AEC_ENGINE" in
    pbfdaf)
        SRCS+=("$SRC_DIR/pbfdaf_aec.c")
        CFLAGS+=(-DATRIOS_AEC_PBFDAF=1 -DATRIOS_AEC_NLMS=0)
        ;;
    nlms)
        CFLAGS+=(-DATRIOS_AEC_PBFDAF=0 -DATRIOS_AEC_NLMS=1)
        ;;
    none)
        CFLAGS+=(-DATRIOS_AEC_PBFDAF=0 -DATRIOS_AEC_NLMS=0)
        ;;
    *)
        echo "Invalid AUDIO_AEC_ENGINE=$AUDIO_AEC_ENGINE (expected: pbfdaf|nlms|none)" >&2
        exit 2
        ;;
esac

case "$AUDIO_NS_ENGINE" in
    gtcrn)
        SRCS+=("$SRC_DIR/gtcrn_ns.c")
        CFLAGS+=(-DATRIOS_NS_GTCRN=1 -DATRIOS_NS_BNR=1)
        LDFLAGS+=(-ldl)
        ;;
    bnr)
        CFLAGS+=(-DATRIOS_NS_GTCRN=0 -DATRIOS_NS_BNR=1)
        ;;
    none)
        CFLAGS+=(-DATRIOS_NS_GTCRN=0 -DATRIOS_NS_BNR=0)
        ;;
    *)
        echo "Invalid AUDIO_NS_ENGINE=$AUDIO_NS_ENGINE (expected: gtcrn|bnr|none)" >&2
        exit 2
        ;;
esac

if [ "$AUDIO_ENABLE_STFT_KALMAN" = "true" ] || [ "$AUDIO_ENABLE_STFT_KALMAN" = "1" ]; then
    CFLAGS+=(-DATRIOS_ENABLE_STFT_KALMAN=1)
else
    CFLAGS+=(-DATRIOS_ENABLE_STFT_KALMAN=0)
fi

mkdir -p "$(dirname "$OUTPUT")"
echo "[audio-dsp] board=$BOARD dev=$HW_CAPTURE_DEVICE ch=$HW_MIC_CHANNELS mics=$AUDIO_VQE_MICS aec=$AUDIO_AEC_ENGINE ns=$AUDIO_NS_ENGINE kalman=$AUDIO_ENABLE_STFT_KALMAN"
"$CC_BIN" "${CFLAGS[@]}" "${SRCS[@]}" "${LDFLAGS[@]}" -o "$OUTPUT"
echo "[audio-dsp] built $OUTPUT ($(wc -c < "$OUTPUT") bytes)"

if [ -n "$STAGE_DIR" ]; then
    mkdir -p "$STAGE_DIR/usr/lib/atrios"
    STAGE_BIN="$STAGE_DIR/usr/lib/atrios/atri-aec"
    if [ "$(realpath -m "$OUTPUT")" != "$(realpath -m "$STAGE_BIN")" ]; then
        install -m 755 "$OUTPUT" "$STAGE_BIN"
    else
        chmod 755 "$STAGE_BIN"
    fi
    if [ "$AUDIO_NS_ENGINE" = "gtcrn" ]; then
        mkdir -p "$STAGE_DIR/usr/share/atrios/models/gtcrn"
        install -m 755 "$OVERLAY_DIR/usr/lib/atrios/libonnxruntime.so" "$STAGE_DIR/usr/lib/atrios/libonnxruntime.so"
        install -m 644 "$OVERLAY_DIR/usr/lib/atrios/LICENSE.onnxruntime" "$STAGE_DIR/usr/lib/atrios/LICENSE.onnxruntime"
        install -m 644 "$OVERLAY_DIR/usr/share/atrios/models/gtcrn/gtcrn_simple.onnx" "$STAGE_DIR/usr/share/atrios/models/gtcrn/gtcrn_simple.onnx"
        install -m 644 "$OVERLAY_DIR/usr/share/atrios/models/gtcrn/LICENSE" "$STAGE_DIR/usr/share/atrios/models/gtcrn/LICENSE"
        echo "[audio-dsp] staged GTCRN runtime + ONNX model into $STAGE_DIR"
    else
        echo "[audio-dsp] skipped GTCRN runtime/model (AUDIO_NS_ENGINE=$AUDIO_NS_ENGINE)"
    fi
fi
