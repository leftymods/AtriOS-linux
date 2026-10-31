#!/system/bin/sh

CURRENT_TIME=`date +%s`
INDEX=$(( CURRENT_TIME%4 ))
while true; do
    if [ -f "/data/quasar/ntpd_sync_enabled" ]; then
        sleep 15
        continue
    fi

    SERVER="${INDEX}.ru.pool.ntp.org"
    /system/vendor/quasar/ntpclient_aarch64 -s -i 10 -c 1 -h ${SERVER}

    date +%s > /data/quasar/data/monotonic_time
    INDEX=$(( (INDEX + 1)%4 ))
    sleep 15
done
