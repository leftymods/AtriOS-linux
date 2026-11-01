#!/bin/sh

check_loopback=1

if [ $# -ge 2 ]; then
    PROCESS_NAME="$1"
    SOUND_FILE="$2"
else
    echo "Specify proccess name that handle audio input on device, and what file to play" 1>&2
    exit 1
fi

adb push ${SOUND_FILE} /data/

PROTO_PLAY="sound_play{sound_file:\"/data/${SOUND_FILE}\", sound_type:CUSTOM, repeat_count: 100}"
echo ${PROTO_PLAY}
adb shell LD_LIBRARY_PATH=/system/vendor/quasar/libs/ /system/vendor/quasar/quasar_client -h localhost -p 9890 -m "'"${PROTO_PLAY}"'" &

# Let metronome start play
sleep 2

PID=`adb shell busybox ps | grep $PROCESS_NAME | awk '{print $1}'`

adb shell kill -USR1 $PID
sleep 4 # collect input data
adb shell kill -USR2 $PID
PROTO_CANCEL='sound_cancel{sound_type:CUSTOM}'
adb shell LD_LIBRARY_PATH=/system/vendor/quasar/libs/ /system/vendor/quasar/quasar_client -h localhost -p 9890 -m "'"${PROTO_CANCEL}"'" &

# get one mic, one spk channel and one sw loopback channel (yandexstation_2 set)
adb pull /mnt/raw_mic_0.raw
adb pull /mnt/raw_spk_1.raw
#ya make
echo "Speaker-mic correlation (positive shift means speaker is earlier):"
./lag_finder --raw1 raw_mic_0.raw --raw2 raw_spk_1.raw
if (( $check_loopback != 0 )) ; then
    adb pull /mnt/lb_0.raw
    echo "Lb-mic correlation (positive shift means Lb is earlier):"
    ./lag_finder --raw1 raw_mic_0.raw --raw2 lb_0.raw
fi

# kill quasar_clients that hang in the background
pkill quasar_client
adb shell pkill quasar_client

# Use Fraunhofer algorithm (require installed octave version >= 5.2)
# sox -r 16000 -e signed-integer -b 16 -c 1 raw_spk_0.raw fraunhofer/spk.wav
# sox -r 16000 -e signed-integer -b 16 -c 1 raw_mic_0.raw fraunhofer/mic.wav
# sox -r 16000 -e signed-integer -b 16 -c 1 lb_0.raw fraunhofer/lb.wav

# cd fraunhofer
# octave main.m
