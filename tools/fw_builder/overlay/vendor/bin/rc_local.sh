#!/system/bin/sh

BOARD=$(busybox awk -F= '/ro.product.model/{print $NF}' /system/build.prop)
echo "BOARD=$BOARD"

currentTime=`date +%s`
controlTime=`getprop ro.build.date.utc`

if [[ $currentTime -le $controlTime ]]; then
   busybox date +%s -s @$controlTime
else
   echo "Time was not changed"
fi

settings put global captive_portal_server 127.0.0.1 &
settings put global captive_portal_detection_enable 0 &

# Trigger I2C modules loading
setprop vendor.load_i2c_modules 1

COUNTER=0
TIMEOUT=3
while [ $COUNTER -lt $TIMEOUT ]; do
   I2C_MODULES_READY=`getprop vendor.i2c_modules_ready | tr -d '\r' `
   if [ "$I2C_MODULES_READY" = "1" ]; then
      break
   fi
   COUNTER=$(($COUNTER+1))
   sleep 1
done

# Start amplifiers setup
/vendor/bin/amplifiers_fw.sh

# Play setup pattern on top LED ring
if [ -x /vendor/quasar/play_pattern.sh ]; then
    /vendor/quasar/play_pattern.sh /vendor/quasar/ledpatterns/configure.led &
fi

# Start Quasar app
/vendor/quasar/onboot.sh

while true; do sleep 65535; done
exit 0
