# AtriOS fish configuration
# Serial console is vt100: fish prints its padding ($<3>) literally, use xterm instead
if test -z "$TERM" -o "$TERM" = vt100 -o "$TERM" = linux -o "$TERM" = dumb
    set -gx TERM xterm
end

# Never run the python man-page completion generator (slow/hangs on this box)
function fish_update_completions; end

set -gx PATH /usr/local/sbin /usr/local/bin /usr/sbin /usr/bin /sbin /bin
set -gx HOME /root
set -gx XDG_RUNTIME_DIR /var/run/pipewire
set -gx DBUS_SESSION_BUS_ADDRESS "unix:path=/var/run/dbus/system_bus_socket"

function fish_greeting
    echo "AtriOS Linux - Yandex Station Max"
    echo "Type 'atrios' to see all commands (screen, ring, audio, wifi, bt, zigbee...)"
end

function fish_title; end

function fish_prompt
    set -l last $status
    set_color brgreen
    printf 'atrios'
    set_color normal
    printf ':'
    set_color brblue
    printf '%s' (string replace -r '^/root' '~' $PWD)
    set_color normal
    if test $last -ne 0
        set_color red
        printf ' [%s]' $last
        set_color normal
    end
    printf '# '
end

function fish_right_prompt; end
