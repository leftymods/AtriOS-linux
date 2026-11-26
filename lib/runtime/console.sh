#!/bin/sh
# Shared parser for the board's kernel console argument and init console device.
# This is data validation; never source /etc/atrios-console as shell code.
atrios_console_parse() {
    ATRIOS_CONSOLE_TTY=${1%%,*}
    ATRIOS_CONSOLE_OPTIONS=
    [ -n "$ATRIOS_CONSOLE_TTY" ] && [ "${#ATRIOS_CONSOLE_TTY}" -le 64 ] || return 1
    case "$ATRIOS_CONSOLE_TTY" in
        [A-Za-z]*) ;;
        *) return 1 ;;
    esac
    case "$ATRIOS_CONSOLE_TTY" in *[!A-Za-z0-9_-]*) return 1 ;; esac
    case "$1" in
        *,*)
            ATRIOS_CONSOLE_OPTIONS=${1#*,}
            # Reject line breaks before the anchored match (grep is line-oriented).
            case "$ATRIOS_CONSOLE_OPTIONS" in *[!0-9neor]*) return 1 ;; esac
            # Linux console options: baud, optional parity/bits and flow control.
            printf '%s\n' "$ATRIOS_CONSOLE_OPTIONS" | LC_ALL=C grep -Eq '^[1-9][0-9]{0,8}([neo][5-8]r?)?$' || return 1
            ;;
    esac
    return 0
}

atrios_console_path() {
    atrios_console_config=${ATRIOS_CONSOLE_CONFIG_FILE:-/etc/atrios-console}
    atrios_console_devices=${ATRIOS_CONSOLE_DEV_DIR:-/dev}
    if [ -r "$atrios_console_config" ]; then
        atrios_console_spec=$(cat "$atrios_console_config") || atrios_console_spec=
        if atrios_console_parse "$atrios_console_spec"; then
            if [ -c "$atrios_console_devices/$ATRIOS_CONSOLE_TTY" ]; then
                printf '%s\n' "$atrios_console_devices/$ATRIOS_CONSOLE_TTY"
                return 0
            fi
        else
            echo '[init] Invalid console configuration; using /dev/console' >&2
        fi
    fi
    printf '%s\n' "$atrios_console_devices/console"
}
