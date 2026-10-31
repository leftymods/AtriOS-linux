#!/system/bin/sh

if [ ! -f /data/quasar/initialized ]; then
    echo "First run. Initialization..."

    echo "Copying config..."
    mkdir -m 777 -p /data/quasar
    mkdir -m 777 -p /data/quasar/logs
    mkdir -m 777 -p /data/quasar/data

    echo "Generating keys..."
    /system/vendor/quasar/gen_keys.sh
fi

mkdir -m 777 -p /tmp/logs

chmod 777 /data/quasar
chmod 777 /data/quasar/logs

# create directory for fluent-bit files
mkdir -p /data/quasar/fluent-bit
# disable fluent-bit, because fluent-bitd should do some work before starting it
/system/vendor/quasar/fluent-bit/set_mode.sh disabled

/system/vendor/quasar/set_monotonic_time.sh

/system/vendor/quasar/q save-last-monotonic-time
/system/vendor/quasar/run.sh

/system/vendor/quasar/ntp_sync.sh &

touch /data/quasar/initialized
