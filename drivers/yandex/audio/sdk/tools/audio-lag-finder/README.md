# How to use

```shell
$ adb push metronome.mp3 /data/
$ adb shell

adb# export LD_LIBRARY_PATH=/system/vendor/quasar/libs/
adb# /system/vendor/quasar/quasar_client -h localhost -p 9890 -m 'sound_play{sound_file:"/data/metronome.mp3", sound_type:CUSTOM, repeat_count: 100}' &
Connected to localhost:9890
adb# PROCESS_NAME=yandexstation
adb# PID=`busybox ps | grep $PROCESS_NAME | busybox awk '{print $1}'`
adb# kill -USR1 $PID
adb# sleep 5
adb# kill -USR2 $PID
adb# /system/vendor/quasar/quasar_client localhost 9890 'sound_cancel{ sound_type:CUSTOM}'
Connected to localhost:9890
adb# killall quasar_client
[2] + Done                 /system/vendor/quasar/quasar_client -h localhost -p 9890 -m "sound_cancel{ sound_type:CUSTOM}"
[1] - Done                 /system/vendor/quasar/quasar_client -h localhost -p 9890 -m "sound_play{sound_file:\"/data/metronome.mp3\", sound_type:CUSTOM, repeat_count: 100}"
adb# exit


$ adb pull /mnt/raw_mic_0.raw
$ adb pull /mnt/raw_spk_0.raw
$ ya make
$ ./lag_finder --raw1 raw_mic_0.raw --raw2 raw_spk_0.raw
```

As a result you will see Shift in seconds
