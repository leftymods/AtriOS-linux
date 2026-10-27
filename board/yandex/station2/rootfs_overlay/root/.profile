# ~/.profile - start fish on interactive login, fall back to sh if fish is broken
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
case "$TERM" in vt100|linux|"") export TERM=xterm ;; esac
if [ -t 0 ] && [ -z "$FISH_VERSION" ] && [ -x /usr/bin/fish ] && /usr/bin/fish -c true >/dev/null 2>&1; then
    export SHELL=/usr/bin/fish
    exec /usr/bin/fish
fi
