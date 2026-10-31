#!/system/bin/sh

if [ "$1" = "enabled" ]; then
    ln -sf /system/vendor/quasar/fluent-bit/logs.cfg /data/quasar/fluent-bit/fb.cfg
elif [ "$1" = "disabled" ]; then
    ln -sf /system/vendor/quasar/fluent-bit/idle.cfg /data/quasar/fluent-bit/fb.cfg
fi
