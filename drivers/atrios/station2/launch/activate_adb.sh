#!/system/bin/sh

. "`dirname $0 `/adb_options.inc" $* 

if [ "$ARG_CMD" = "activate" ]; then
    getprop persist.sys.usb.config | grep adb > /dev/null && exit 0
    setprop service.adb.tcp.port 5555
    setprop sys.usb.config adb
    setprop persist.sys.usb.config adb

    echo "`date`: Start adb" >> "${ADB_LOG}"
else
    setprop persist.sys.usb.config none
    pkill adbd
    echo "`date`: Stop adb" >> "${ADB_LOG}"
fi


