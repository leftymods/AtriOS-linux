#!/bin/sh
# AtriOS live fix for the Yandex Station Max audio path and clock display.
#
# 1. Draw a single centered colon on the 25x16 front matrix (the old
#    0b101 glyph produced two dot columns and looked like "::").
# 2. Report/raise the 20V PVDD rail.  The DTS fixed regulator "20V_AMPL"
#    owns GPIOX_10, so normally we only inspect it; the sysfs fallback is
#    used when the regulator driver is absent.
# 3. Route the TDM_B SCLK/LRCLK/MCLK pads to mst_b (GUIDE.md, L41).
# 4. Apply the FRDDR_A -> TDMOUT_B / TODDR_A -> PDM routing and unmute
#    the SY6045S amplifiers.
#
# Run as root on the device:  sh atrios-audio-screen-fix.sh
set -u

CARD="AMLAUGESOUND"

echo "[1/4] Fixing the clock colon..."
for f in /usr/lib/atrios/screend /usr/lib/atrios/light; do
    [ -f "$f" ] || continue
    sed -i 's/CLOCK_COLON = \[0b000, 0b101, 0b101, 0b000, 0b101, 0b101, 0b000\]/CLOCK_COLON = [0b000, 0b010, 0b010, 0b000, 0b010, 0b010, 0b000]/' "$f"
    # Move the two hour digits one LED left so the gap before the colon grows
    # by one pixel while the minute digits stay at x=15/20.
    sed -i 's/x_positions = \[1, 7, 12, 15, 20\]/x_positions = [0, 6, 12, 15, 20]/' "$f"
done
pkill -f /usr/lib/atrios/screend 2>/dev/null || true
sleep 1
/usr/lib/atrios/screend >/var/log/atrios-screend.log 2>&1 &
echo "      clock daemon restarted"

echo "[2/4] 20V PVDD rail..."
rail_reg=""
for r in /sys/class/regulator/regulator.*; do
    [ -f "$r/name" ] || continue
    if [ "$(cat "$r/name" 2>/dev/null)" = "20V_AMPL" ]; then
        rail_reg="$r"
        break
    fi
done
if [ -n "$rail_reg" ]; then
    echo "      ${rail_reg##*/}: state=$(cat "$rail_reg/state" 2>/dev/null) uV=$(cat "$rail_reg/microvolts" 2>/dev/null)"
else
    echo "      no 20V_AMPL regulator; using gpio-485 fallback"
    [ -d /sys/class/gpio/gpio485 ] || echo 485 > /sys/class/gpio/export 2>/dev/null || true
    if [ -d /sys/class/gpio/gpio485 ]; then
        echo out > /sys/class/gpio/gpio485/direction 2>/dev/null || true
        echo 1 > /sys/class/gpio/gpio485/value 2>/dev/null || true
        echo "      gpio485=$(cat /sys/class/gpio/gpio485/value 2>/dev/null)"
    fi
fi

echo "[3/4] Routing TDM_B clock pads to mst_b..."
python3 -c '
import mmap, os, struct
fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
# SM1 audio clock block (S905X3).  SM1 pad layout: CTRL0=0x20 (MCLK),
# CTRL1=0x24 (SCLK/LRCLK).  The G12A guide uses 0x1C/0x20, not this SoC.
m = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=0xFF660000)
struct.pack_into("<I", m, 0x24, 0x01110111)  # SCLK/LRCLK -> mst_b
struct.pack_into("<I", m, 0x20, 0x00000011)  # MCLK -> mst_b
m.flush()
os.close(fd)
print("      pad mux OK")
' || echo "      pad mux write failed (is /dev/mem available?)"

echo "[4/4] ALSA routing and amplifier unmute..."
i=0
while ! grep -qi AUGESOUND /proc/asound/cards 2>/dev/null && [ "$i" -lt 10 ]; do
    sleep 1
    i=$((i+1))
done
if command -v amixer >/dev/null 2>&1; then
    amixer -c "$CARD" cset name='Audio Out Sink' 1 2>/dev/null || true
    amixer -c "$CARD" cset name='SPK mute' 0 2>/dev/null || true
    # Per-channel 0 dB trims only.  The master volume comes from the
    # AtriOS state file, never from this helper.
    for ch in 'Woofer Ch1' 'Woofer Ch2' 'Tweeters Ch1' 'Tweeters Ch2'; do
        amixer -c "$CARD" cset name="${ch} Playback Switch" 1 2>/dev/null || true
        amixer -c "$CARD" cset name="${ch} Playback Volume" 159 2>/dev/null || true
    done
fi
if [ -x /usr/lib/atrios/audio ]; then
    /bin/sh /usr/lib/atrios/audio apply 2>/dev/null || true
fi
/etc/init.d/S03amplifiers start 2>/dev/null || true

echo
echo "Done.  The clock should now read HH:MM with one centered colon."
echo "Test sound with:  atrios play"
echo "NOTE: the old 'atrios test' still contains the old GPIO-only rail check;"
echo "      the corrected check is included in the v113 image."
