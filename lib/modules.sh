#!/bin/bash
# ==============================================================================
# AtriOS Build System - System Module Pruner & Manifest Generator
# ==============================================================================
# Removes disabled subsystems from a staged rootfs and writes
# /etc/atrios/hardware.json + /etc/atrios/modules.json so runtime services and
# companion apps know the exact OS distro, AtriOS app flavor, DSP engines, and
# enabled system modules on this speaker.

_mod_is_false() {
    case "${1:-true}" in
        false|0|no|off|none) return 0 ;;
        *) return 1 ;;
    esac
}

_mod_rm() {
    local target="$1"
    if [ -e "$target" ] || [ -L "$target" ]; then
        if [ "$(id -u)" -eq 0 ]; then
            rm -rf -- "$target"
        elif command -v sudo >/dev/null 2>&1 && [ ! -w "$(dirname "$target")" ]; then
            sudo rm -rf -- "$target"
        else
            rm -rf -- "$target"
        fi
    fi
}

_mod_write_file() {
    local dest="$1"
    local parent
    parent="$(dirname "$dest")"
    if [ "$(id -u)" -eq 0 ] || [ -w "$parent" ] || { [ ! -e "$parent" ] && mkdir -p "$parent" 2>/dev/null; }; then
        mkdir -p "$parent"
        cat > "$dest"
    else
        sudo mkdir -p "$parent"
        sudo tee "$dest" >/dev/null
    fi
}

prune_disabled_rootfs_modules() {
    local stage_dir="$1"
    [ -d "$stage_dir" ] || exit_with_error "Stage directory does not exist: $stage_dir"

    if _mod_is_false "${MOD_ATRIAI:-true}"; then
        _mod_rm "$stage_dir/etc/init.d/S80atriai"
        _mod_rm "$stage_dir/usr/lib/atrios/atriai"
    fi

    if _mod_is_false "${MOD_YMUSIC:-true}"; then
        _mod_rm "$stage_dir/usr/lib/atrios/ymusic"
        _mod_rm "$stage_dir/usr/bin/atri-mp3dec"
    fi

    if _mod_is_false "${MOD_BLUETOOTH:-true}"; then
        _mod_rm "$stage_dir/etc/init.d/S71atrios-bt-control"
        _mod_rm "$stage_dir/usr/lib/atrios/bt-agent"
        _mod_rm "$stage_dir/usr/lib/atrios/bt-control"
        _mod_rm "$stage_dir/usr/lib/atrios/bt-volume-sync"
        _mod_rm "$stage_dir/usr/lib/atrios/rtk-bt-bridge"
        _mod_rm "$stage_dir/usr/lib/atrios/rtk-bt-init"
    fi

    if _mod_is_false "${MOD_PIPEWIRE:-true}"; then
        _mod_rm "$stage_dir/etc/init.d/S50pipewire"
    fi

    if _mod_is_false "${MOD_DISPLAY:-true}"; then
        _mod_rm "$stage_dir/usr/lib/atrios/screend"
    fi

    if _mod_is_false "${MOD_ROTARY:-true}"; then
        _mod_rm "$stage_dir/usr/lib/atrios/rotaryd"
    fi

    if _mod_is_false "${MOD_LIGHT:-true}"; then
        _mod_rm "$stage_dir/usr/lib/atrios/light"
    fi

    if _mod_is_false "${MOD_OTA:-true}"; then
        _mod_rm "$stage_dir/etc/init.d/S85atrios-update"
        _mod_rm "$stage_dir/etc/init.d/S04atrios-ota-recover"
        _mod_rm "$stage_dir/usr/lib/atrios/ota"
    fi

    # Strip heavy ONNX Runtime + GTCRN neural model when board does not use gtcrn
    if [ "${AUDIO_NS_ENGINE:-gtcrn}" != "gtcrn" ]; then
        _mod_rm "$stage_dir/usr/lib/atrios/libonnxruntime.so"
        _mod_rm "$stage_dir/usr/lib/atrios/LICENSE.onnxruntime"
        _mod_rm "$stage_dir/usr/share/atrios/models/gtcrn"
    fi
}

write_hardware_manifest() {
    local stage_dir="$1"
    _mod_write_file "$stage_dir/etc/atrios/hardware.json" <<HWEOF
{
  "schema": 1,
  "board": "${BOARD}",
  "name": "${BOARD_NAME:-AtriOS Speaker}",
  "vendor": "${VENDOR:-generic}",
  "soc": "${SOC:-unknown}",
  "dtb": "${DTS_NAME}.dtb",
  "display": {
    "type": "${HW_DISPLAY_TYPE:-none}",
    "width": ${HW_DISPLAY_WIDTH:-0},
    "height": ${HW_DISPLAY_HEIGHT:-0}
  },
  "led_ring": {
    "count": ${HW_RING_LEDS:-0}
  },
  "audio": {
    "amp_i2c_bus": ${HW_AMP_I2C_BUS:-2},
    "amp_tweeter_addr": "${HW_AMP_TWEETER_ADDR:-0x2a}",
    "amp_woofer_addr": "${HW_AMP_WOOFER_ADDR:-0x2b}",
    "mic_channels": ${HW_MIC_CHANNELS:-0},
    "capture_device": "${HW_CAPTURE_DEVICE:-hw:0,3}"
  },
  "uarts": {
    "bluetooth": "${HW_BT_UART:-/dev/ttyAML1}",
    "zigbee": "${HW_ZIGBEE_UART:-/dev/ttyAML2}"
  },
  "features": {
    "rotary": ${HW_HAS_ROTARY:-false},
    "als": ${HW_HAS_ALS:-false},
    "zigbee": ${HW_HAS_ZIGBEE:-false},
    "bluetooth": ${HW_HAS_BT:-true}
  }
}
HWEOF
}

write_modules_manifest() {
    local stage_dir="$1"
    _mod_write_file "$stage_dir/etc/atrios/modules.json" <<MODEOF
{
  "schema": 1,
  "board": "${BOARD}",
  "profile": "${ATRIOS_PROFILE:-full}",
  "rootfs": {
    "distro": "${ROOTFS_DISTRO:-debian}",
    "suite": "${ROOTFS_SUITE:-bookworm}",
    "shell": "${ROOTFS_SHELL:-fish}",
    "partition_sectors": ${ROOTFS_PARTITION_SIZE:-2097152}
  },
  "app": {
    "flavor": "${ATRIOS_APP_FLAVOR:-full}"
  },
  "audio_dsp": {
    "capture_device": "${HW_CAPTURE_DEVICE:-hw:0,3}",
    "mic_channels": ${HW_MIC_CHANNELS:-8},
    "vqe_mics": ${AUDIO_VQE_MICS:-7},
    "aec_engine": "${AUDIO_AEC_ENGINE:-pbfdaf}",
    "ns_engine": "${AUDIO_NS_ENGINE:-gtcrn}",
    "stft_kalman": ${AUDIO_ENABLE_STFT_KALMAN:-true}
  },
  "modules": {
    "atriai": ${MOD_ATRIAI:-true},
    "ymusic": ${MOD_YMUSIC:-true},
    "bluetooth": ${MOD_BLUETOOTH:-true},
    "pipewire": ${MOD_PIPEWIRE:-true},
    "display": ${MOD_DISPLAY:-true},
    "rotary": ${MOD_ROTARY:-true},
    "light": ${MOD_LIGHT:-true},
    "zigbee": ${MOD_ZIGBEE:-true},
    "ota": ${MOD_OTA:-true}
  }
}
MODEOF
}
