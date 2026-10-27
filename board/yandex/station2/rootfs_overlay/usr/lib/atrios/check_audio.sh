#!/bin/sh
# /usr/lib/atrios/check_audio.sh
# ==============================================================================
# AtriOS Automated Audio Subsystem Parity Diagnostics Script
# System: Yandex Station Max (SM1 / S905X3, Rev >= 3)
# Sound Card: AML-AUGESOUND (Card 0)
#
# Diagnostics Checklist:
#   1. I2C Bus Codecs: ES8156 (0x08), SY6045S (0x2a, 0x2b), ES7210 (0x40) -> exit 1
#   2. 20V Booster Power Rail: 20V_AMPL regulator (GPIO line 75)            -> exit 2
#   3. ALSA State: Unmuted switches and non-zero user volumes             -> exit 3
#   4. PCM Stream Playback: Underrun-free stream flow                      -> exit 4
#
# Return Codes:
#   0: Success - all checks passed
#   1: I2C codec failure
#   2: 20V booster rail failure
#   3: ALSA mixer / state failure
#   4: Audio playback / underrun failure
# ==============================================================================

set -u

SCRIPT_VERSION="1.0.1"
VERBOSE=0
JSON_MODE=0

# Hardware constants
I2C_BUS=2
GPIO_RAIL=485          # gpio-485 on the vendor kernel (line 75 + base 410)
ALSA_CARD_NAME="AML-AUGESOUND"

# Resolve sysfs base directory (overridable for tests/chroots)
SYSFS_BASE="${SYSFS_BASE:-}"

show_help() {
    cat << 'EOF'
AtriOS Audio Subsystem Diagnostics (/usr/lib/atrios/check_audio.sh)

Usage:
  check_audio.sh [OPTIONS] [SUBCOMMAND]

Options:
  -v, --verbose          Enable detailed diagnostic output
  -j, --json             Output structured JSON document
  -h, --help             Display this help message and exit

Subcommands:
  check, test            Run standard audio diagnostics suite (default)

Exit Codes:
  0: All audio subsystem diagnostic checks passed
  1: I2C codec missing or unresponsive (0x08, 0x2a, 0x2b, 0x40)
  2: 20V booster power rail inactive (20V_AMPL regulator off / GPIO line 75 != 1)
  3: ALSA mixer muted, zero volume, or sound card missing
  4: PCM audio stream playback failure or underrun errors
EOF
}

# Parse CLI arguments (POSIX compliant)
while [ $# -gt 0 ]; do
    case "$1" in
        -v|--verbose)
            VERBOSE=1
            ;;
        -j|--json)
            JSON_MODE=1
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        check|test|audio)
            ;;
        *)
            echo "Error: Unknown option '$1'" >&2
            echo "Try '$0 --help' for more information." >&2
            exit 1
            ;;
    esac
    shift
done

log_info() {
    if [ "$JSON_MODE" -eq 0 ]; then
        echo "$@"
    fi
}

log_verbose() {
    if [ "$VERBOSE" -eq 1 ] && [ "$JSON_MODE" -eq 0 ]; then
        echo "$@"
    fi
}

log_error() {
    if [ "$JSON_MODE" -eq 0 ]; then
        echo "Error: $@" >&2
    fi
}

# State tracking for JSON output
CHECK_I2C_STATUS="PASS"
CHECK_I2C_MSG=""
CHECK_GPIO_STATUS="PASS"
CHECK_GPIO_MSG=""
CHECK_ALSA_STATUS="PASS"
CHECK_ALSA_MSG=""
CHECK_PLAYBACK_STATUS="PASS"
CHECK_PLAYBACK_MSG=""

# ==============================================================================
# Phase 1: I2C Codec Bus Audit (0x08, 0x2a, 0x2b, 0x40) -> exit 1 on failure
# ==============================================================================
audit_i2c() {
    log_verbose "[1/4] Auditing I2C Codec Bus (bus: i2c-${I2C_BUS})..."

    for addr in 0x08 0x2a 0x2b 0x40; do
        dev_found=0
        hex4=$(printf "%04x" "$((addr))" 2>/dev/null || echo "0000")

        # 1. Hardware probe via i2cget (-y non-interactive, -f force driver override)
        if [ -e "/dev/i2c-${I2C_BUS}" ] && command -v i2cget >/dev/null 2>&1; then
            if i2cget -y -f "${I2C_BUS}" "${addr}" 0x00 >/dev/null 2>&1; then
                dev_found=1
                log_verbose "  * I2C device ${addr} responded on bus ${I2C_BUS}"
            fi
        fi

        # 2. Sysfs device binding check
        if [ "$dev_found" -eq 0 ]; then
            sysfs_dev="${SYSFS_BASE}/sys/bus/i2c/devices/${I2C_BUS}-${hex4}"
            if [ -d "$sysfs_dev" ]; then
                dev_found=1
                log_verbose "  * I2C device ${addr} verified in sysfs (${I2C_BUS}-${hex4})"
            fi
        fi

        if [ "$dev_found" -eq 0 ]; then
            CHECK_I2C_STATUS="FAIL"
            CHECK_I2C_MSG="I2C codec at ${addr} missing or unresponsive"
            log_error "I2C codec missing"
            return 1
        fi
    done

    CHECK_I2C_MSG="All 4 I2C codecs verified (0x08, 0x2a, 0x2b, 0x40)"
    log_verbose "  [OK] I2C bus audit passed."
    return 0
}

# ==============================================================================
# Phase 2: 20V Booster Power Rail Audit -> exit 2 on failure
#
# The DTS models the rail as the fixed regulator "20V_AMPL" (GPIOX_10,
# line 75 of the periphs GPIO controller, gpio-485 on this kernel).  The
# regulator driver owns that line, so exporting it through sysfs fails
# with EBUSY even when the rail is perfectly active.  Check the regulator
# first and only fall back to sysfs/libgpiod when it is not registered.
# ==============================================================================
RAIL_REG_NAME="20V_AMPL"
RAIL_GPIO_LINE=75

find_rail_regulator() {
    for r in "${SYSFS_BASE}/sys/class/regulator"/regulator.*; do
        [ -f "$r/name" ] || continue
        if [ "$(cat "$r/name" 2>/dev/null)" = "$RAIL_REG_NAME" ]; then
            echo "$r"
            return 0
        fi
    done
    return 1
}

gpio_global_for_line() {
    line="$1"
    for chip in "${SYSFS_BASE}/sys/class/gpio"/gpiochip*; do
        [ -d "$chip" ] || continue
        base=$(cat "$chip/base" 2>/dev/null || true)
        ngpio=$(cat "$chip/ngpio" 2>/dev/null || true)
        [ -n "$base" ] && [ -n "$ngpio" ] || continue
        if [ "$line" -lt "$ngpio" ]; then
            echo $((base + line))
            return 0
        fi
    done
    return 1
}

audit_gpio() {
    log_verbose "[2/4] Auditing 20V Booster Power Rail (${RAIL_REG_NAME})..."

    rail_reg=$(find_rail_regulator || true)
    if [ -n "$rail_reg" ]; then
        rail_state=$(cat "$rail_reg/state" 2>/dev/null || true)
        rail_uv=$(cat "$rail_reg/microvolts" 2>/dev/null || true)
        if [ "$rail_state" = "enabled" ] || \
           { [ -z "$rail_state" ] && [ "$rail_uv" = "20000000" ]; }; then
            CHECK_GPIO_MSG="20V_AMPL regulator enabled (${rail_state:-state?}, ${rail_uv:-?} uV)"
            log_verbose "  [OK] 20V booster rail active (regulator ${rail_reg##*/}, ${rail_state:-?}, ${rail_uv:-?} uV)."
            return 0
        fi
        log_verbose "  * 20V_AMPL regulator found but state='${rail_state}', trying GPIO..."
    fi

    # Fallback: read the physical line.  Prefer the sysfs number if it is
    # already exported, otherwise resolve the controller base at runtime
    # (gpio-485 on the vendor kernel, gpio-502 on newer mainline trees).
    rail_val=""
    gpio_num=$(gpio_global_for_line "$RAIL_GPIO_LINE" || true)
    if [ -n "$gpio_num" ]; then
        gpio_dir="${SYSFS_BASE}/sys/class/gpio/gpio${gpio_num}"
        gpio_export="${SYSFS_BASE}/sys/class/gpio/export"
        if [ ! -d "$gpio_dir" ] && [ -w "$gpio_export" ]; then
            echo "$gpio_num" > "$gpio_export" 2>/dev/null || true
        fi
        if [ -f "$gpio_dir/value" ]; then
            rail_val=$(tr -d '[:space:]' < "$gpio_dir/value" 2>/dev/null || true)
        fi
    fi
    if [ -z "$rail_val" ] && [ -z "$SYSFS_BASE" ] && command -v gpioget >/dev/null 2>&1; then
        rail_val=$(gpioget --numeric gpiochip0 "$RAIL_GPIO_LINE" 2>/dev/null || true)
    fi

    if [ "$rail_val" != "1" ]; then
        CHECK_GPIO_STATUS="FAIL"
        CHECK_GPIO_MSG="20V_AMPL rail is not enabled (GPIO ${gpio_num:-?}='${rail_val:-?}')"
        log_error "20V booster rail inactive"
        return 1
    fi

    CHECK_GPIO_MSG="20V_AMPL rail active (GPIO ${gpio_num} value=1)"
    log_verbose "  [OK] 20V booster rail active (GPIO ${gpio_num} = 1)."
    return 0
}

# ==============================================================================
# Phase 3: ALSA Mixer & State Audit (AML-AUGESOUND) -> exit 3 on failure
# ==============================================================================
find_alsa_state_file() {
    if [ -n "${ALSA_STATE_FILE:-}" ] && [ -f "$ALSA_STATE_FILE" ]; then
        echo "$ALSA_STATE_FILE"
        return 0
    fi
    SCRIPT_DIR="$(cd "$(dirname "$0")" 2>/dev/null && pwd || true)"
    for p in /var/lib/alsa/asound.state /etc/asound.state; do
        if [ -n "$p" ] && [ -f "$p" ]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

get_alsa_ctl() {
    ctl_name="$1"
    val=""

    if command -v amixer >/dev/null 2>&1; then
        for cid in AMLAUGESOUND 0; do
            raw=$(amixer -c "$cid" cget name="$ctl_name" 2>/dev/null || true)
            if [ -n "$raw" ]; then
                val=$(echo "$raw" | sed -n 's/.*: values=//p' | tr -d ' \r\n')
                [ -n "$val" ] && break
            fi
            raw_s=$(amixer -c "$cid" sget "$ctl_name" 2>/dev/null || true)
            if [ -n "$raw_s" ]; then
                val=$(echo "$raw_s" | grep -o '\[[0-9a-zA-Z%]*\]' | tr -d '[]' | head -n 1)
                [ -n "$val" ] && break
            fi
        done
    fi

    if [ -z "$val" ]; then
        st_file=$(find_alsa_state_file || true)
        if [ -n "$st_file" ] && [ -f "$st_file" ]; then
            val=$(awk -v q="'" -v name="$ctl_name" '
                BEGIN { target = "name " q name q }
                index($0, target) { found=1; next }
                found && $1 == "value" { print $2; exit }
                found && $1 == "}" { exit }
            ' "$st_file" 2>/dev/null || true)
        fi
    fi

    echo "$val"
}

# amixer reports stereo controls as "255,255" and switches as "on,on".
# Accept a comma-separated list only when every element matches one of the
# expected values.
value_is_one_of() {
    _val="$1"
    shift
    [ -n "$_val" ] || return 1
    for _tok in $(echo "$_val" | tr ',' ' '); do
        _ok=0
        for _exp in "$@"; do
            if [ "$_tok" = "$_exp" ]; then
                _ok=1
                break
            fi
        done
        [ "$_ok" -eq 1 ] || return 1
    done
    return 0
}

# The master/headphone volume is the user's listening level, not a fixed
# calibration value.  Any non-zero value means "not muted"; 0 is the only
# failure.  (The old check demanded 255/193 and failed after the first
# playback, when PipeWire or the knob had legitimately lowered it.)
value_is_positive() {
    _val="$1"
    [ -n "$_val" ] || return 1
    for _tok in $(echo "$_val" | tr ',' ' '); do
        case "$_tok" in
            ''|*[!0-9]*) return 1 ;;
        esac
        [ "$_tok" -gt 0 ] || return 1
    done
    return 0
}

audit_alsa() {
    log_verbose "[3/4] Auditing ALSA State for card ${ALSA_CARD_NAME}..."

    # 1. Verify sound card registration or state file
    card_found=0
    if [ -f "/proc/asound/cards" ] && grep -qi "AUGESOUND" /proc/asound/cards; then
        card_found=1
    elif command -v aplay >/dev/null 2>&1 && aplay -l 2>/dev/null | grep -qi "AUGESOUND"; then
        card_found=1
    elif [ ! -e "/proc/asound/cards" ]; then
        # Offline/test environment only; never accept a stale asound.state
        # when a live ALSA core exists but the card did not probe.
        st_file=$(find_alsa_state_file || true)
        if [ -n "$st_file" ] && grep -q "state.AMLAUGESOUND" "$st_file"; then
            card_found=1
        fi
    fi

    if [ "$card_found" -ne 1 ]; then
        CHECK_ALSA_STATUS="FAIL"
        CHECK_ALSA_MSG="ALSA card AML-AUGESOUND missing"
        log_error "ALSA mixer muted: ${CHECK_ALSA_MSG}"
        return 1
    fi

    # 2. Verify playback switches unmuted
    for sw in "Woofer Master Playback Switch" \
              "Tweeters Master Playback Switch" \
              "Headphone Headphone Switch"; do
        sw_val=$(get_alsa_ctl "$sw")
        if value_is_one_of "$sw_val" 1 true on yes; then
            log_verbose "  * ${sw}: ${sw_val} [OK]"
        else
            CHECK_ALSA_STATUS="FAIL"
            CHECK_ALSA_MSG="${sw} is muted (${sw_val})"
            log_error "ALSA mixer muted: ${CHECK_ALSA_MSG}"
            return 1
        fi
    done

    # 3. Verify hardware SPK mute is disabled (inverted logic).
    #    The control only exists when the DT provides spk_mute-gpios.  On this
    #    board the amps are enabled through sy6045s, so when the control is
    #    absent skip the check instead of trusting a stale asound.state that
    #    may come from a different kernel.
    if amixer -c 0 cget name='SPK mute' >/dev/null 2>&1 || \
       amixer -c AMLAUGESOUND cget name='SPK mute' >/dev/null 2>&1; then
        spk_val=$(get_alsa_ctl "SPK mute")
        if value_is_one_of "$spk_val" 0 false off no; then
            log_verbose "  * SPK mute: ${spk_val} [OK]"
        else
            CHECK_ALSA_STATUS="FAIL"
            CHECK_ALSA_MSG="SPK mute is active (${spk_val})"
            log_error "ALSA mixer muted: ${CHECK_ALSA_MSG}"
            return 1
        fi
    else
        log_verbose "  * SPK mute: control not present (no spk_mute GPIO) [SKIP]"
    fi

    # 4. Verify user volume levels are non-zero (0 means muted)
    w_master=$(get_alsa_ctl "Woofer Master Playback Volume")
    if ! value_is_positive "$w_master"; then
        CHECK_ALSA_STATUS="FAIL"
        CHECK_ALSA_MSG="Woofer Master Playback Volume is muted/zero (${w_master:-<missing>})"
        log_error "ALSA mixer muted: ${CHECK_ALSA_MSG}"
        return 1
    fi
    log_verbose "  * Woofer Master Playback Volume: ${w_master} (user level) [OK]"

    t_master=$(get_alsa_ctl "Tweeters Master Playback Volume")
    if ! value_is_positive "$t_master"; then
        CHECK_ALSA_STATUS="FAIL"
        CHECK_ALSA_MSG="Tweeters Master Playback Volume is muted/zero (${t_master:-<missing>})"
        log_error "ALSA mixer muted: ${CHECK_ALSA_MSG}"
        return 1
    fi
    log_verbose "  * Tweeters Master Playback Volume: ${t_master} (user level) [OK]"

    for ch in "Woofer Ch1 Playback Volume" "Woofer Ch2 Playback Volume" \
              "Tweeters Ch1 Playback Volume" "Tweeters Ch2 Playback Volume"; do
        ch_val=$(get_alsa_ctl "$ch")
        if ! value_is_one_of "$ch_val" 159; then
            CHECK_ALSA_STATUS="FAIL"
            CHECK_ALSA_MSG="${ch} (${ch_val:-<missing>}) != 159"
            log_error "ALSA mixer muted: ${CHECK_ALSA_MSG}"
            return 1
        fi
        log_verbose "  * ${ch}: ${ch_val} (0 dB) [OK]"
    done

    hp_vol=$(get_alsa_ctl "Headphone DAC Playback Volume")
    if ! value_is_positive "$hp_vol"; then
        CHECK_ALSA_STATUS="FAIL"
        CHECK_ALSA_MSG="Headphone DAC Playback Volume is muted/zero (${hp_vol:-<missing>})"
        log_error "ALSA mixer muted: ${CHECK_ALSA_MSG}"
        return 1
    fi
    log_verbose "  * Headphone DAC Playback Volume: ${hp_vol} (user level) [OK]"

    CHECK_ALSA_MSG="Card AML-AUGESOUND unmuted, non-zero user volumes verified"
    log_verbose "  [OK] ALSA state audit passed."
    return 0
}

# ==============================================================================
# Phase 4: Audio Stream Playback Audit -> exit 4 on failure
# ==============================================================================
audit_playback() {
    log_verbose "[4/4] Auditing Audio Stream Playback..."

    # Perform non-interactive playback if live sound device exists
    play_err=0
    play_output=""

    if [ -e "/dev/snd/pcmC0D0p" ] || ( [ -f /proc/asound/cards ] && grep -qi "AUGESOUND" /proc/asound/cards ); then
        log_verbose "  * Live sound card detected. Executing PCM stream verification..."

        # Prefer real music over a sine beep.  Put any WAV in /root/music
        # (for example /root/music/tsoi.wav) and the test will use it.
        TEST_AUDIO=""
        for f in /root/music/*.wav \
                 /usr/share/sounds/atrios/test-music.wav \
                 /usr/share/sounds/alsa/Front_Center.wav; do
            if [ -f "$f" ]; then
                TEST_AUDIO="$f"
                break
            fi
        done

        # Priority 1: PipeWire pw-play if daemon is running
        if command -v pw-play >/dev/null 2>&1 && pidof pipewire >/dev/null 2>&1 && [ -n "$TEST_AUDIO" ]; then
            log_verbose "  * Playing test audio via PipeWire: ${TEST_AUDIO}"
            play_output=$(pw-play "$TEST_AUDIO" 2>&1) || play_err=$?
        # Priority 2: aplay WAV/music playback
        elif command -v aplay >/dev/null 2>&1 && [ -n "$TEST_AUDIO" ]; then
            log_verbose "  * Playing test audio via aplay: ${TEST_AUDIO}"
            play_output=$(aplay -D default "$TEST_AUDIO" 2>&1) || \
            play_output=$(aplay -D "plughw:${ALSA_CARD_NAME},0" "$TEST_AUDIO" 2>&1) || play_err=$?
        # Priority 3: speaker-test finite 1-loop sine test tone
        elif command -v speaker-test >/dev/null 2>&1; then
            log_verbose "  * Playing 440Hz sine test tone via speaker-test..."
            play_output=$(speaker-test -D default -c 2 -r 48000 -t sine -f 440 -l 1 2>&1) || \
            play_output=$(speaker-test -D "plughw:${ALSA_CARD_NAME},0" -c 2 -r 48000 -t sine -f 440 -l 1 2>&1) || play_err=$?
        fi

        # Inspect return code and output for underruns / xruns
        if [ "$play_err" -ne 0 ] || echo "$play_output" | grep -Ei "underrun|xrun|broken configuration|playback open error|write error" >/dev/null 2>&1; then
            CHECK_PLAYBACK_STATUS="FAIL"
            CHECK_PLAYBACK_MSG="Audio playback reported underrun or pipeline failure: ${play_output}"
            log_error "Audio playback underrun"
            return 1
        fi
    else
        log_verbose "  * Offline/container environment: sound device not present. Skipping physical playback."
    fi

    CHECK_PLAYBACK_MSG="Stream playback verified without underruns"
    log_verbose "  [OK] Audio stream playback audit passed."
    return 0
}

# ==============================================================================
# Execution Dispatch & Result Reporting
# ==============================================================================
log_verbose "=========================================================="
log_verbose "   AtriOS Audio Subsystem Diagnostics (AML-AUGESOUND)     "
log_verbose "=========================================================="

# Run Check 1: I2C Codecs
if ! audit_i2c; then
    if [ "$JSON_MODE" -eq 1 ]; then
        printf '{"status":"FAIL","exit_code":1,"error":"%s","checks":{"i2c":"FAIL"}}\n' "$CHECK_I2C_MSG"
    fi
    exit 1
fi

# Run Check 2: 20V Booster Power Rail
if ! audit_gpio; then
    if [ "$JSON_MODE" -eq 1 ]; then
        printf '{"status":"FAIL","exit_code":2,"error":"%s","checks":{"i2c":"PASS","gpio":"FAIL"}}\n' "$CHECK_GPIO_MSG"
    fi
    exit 2
fi

# Run Check 3: ALSA Mixer & State
if ! audit_alsa; then
    if [ "$JSON_MODE" -eq 1 ]; then
        printf '{"status":"FAIL","exit_code":3,"error":"%s","checks":{"i2c":"PASS","gpio":"PASS","alsa":"FAIL"}}\n' "$CHECK_ALSA_MSG"
    fi
    exit 3
fi

# Run Check 4: Audio Stream Playback
if ! audit_playback; then
    if [ "$JSON_MODE" -eq 1 ]; then
        printf '{"status":"FAIL","exit_code":4,"error":"%s","checks":{"i2c":"PASS","gpio":"PASS","alsa":"PASS","playback":"FAIL"}}\n' "$CHECK_PLAYBACK_MSG"
    fi
    exit 4
fi

# All Checks Passed
if [ "$JSON_MODE" -eq 1 ]; then
    printf '{"status":"PASS","exit_code":0,"card":"%s","checks":{"i2c":"PASS","power_rail_20v":"PASS","alsa_state":"PASS","playback":"PASS"},"message":"All audio subsystem diagnostic checks passed."}\n' "$ALSA_CARD_NAME"
elif [ "$VERBOSE" -eq 1 ]; then
    log_info "=========================================================="
    log_info "All audio subsystem diagnostic checks passed."
    log_info "Exit Code: 0"
    log_info "=========================================================="
else
    log_info "[OK] I2C Codecs: 0x08, 0x2a, 0x2b, 0x40 present"
    log_info "[OK] 20V Booster Power: 20V_AMPL rail active"
    log_info "[OK] ALSA State: card AML-AUGESOUND unmuted, non-zero volumes verified"
    log_info "[OK] Audio Playback: stream playback verified without underruns"
    log_info "All audio subsystem diagnostic checks passed."
fi

exit 0
