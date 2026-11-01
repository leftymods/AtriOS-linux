#!/system/bin/sh

while true
do
	NOW=`date`
    TEMPER=`cat /sys/class/thermal/thermal_zone0/temp`
    echo "$NOW Temperature : $TEMPER"

    LA=`cat /proc/loadavg | busybox awk {'print $1'}`
    echo "$NOW Load average: $LA"

    FAIRLOAD=`top -t -n 1 | grep " R " | wc -l`
    echo "$NOW Fair load: $FAIRLOAD"

    CPULOAD=`top -n 1 | grep User | grep System | busybox awk {'print $2+$4+$6+$8'}`
    echo "$NOW CPU load: $CPULOAD"

    CPU_EXT=`top -n 1 | grep User | grep Nice`
    echo "$NOW CPU extended: $CPU_EXT"

    IOW=`top -n 1 | grep User | grep Nice | busybox awk '{print $14}'`
    echo "$NOW IOW: $IOW"

    CPUFREQ0=`cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`
    CPUFREQ1=`cat /sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq`
    CPUFREQ2=`cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_cur_freq`
    CPUFREQ3=`cat /sys/devices/system/cpu/cpu3/cpufreq/scaling_cur_freq`

    echo "$NOW CPU freqs: $CPUFREQ0 $CPUFREQ1 $CPUFREQ2 $CPUFREQ3"

    THREADS=`ps -t | wc -l`
    echo "$NOW Threads: $THREADS"

    AVAILABLE=`cat /proc/meminfo | grep -E "MemFree|Buffers|Cached" | busybox awk '{s+=$2} END {print s}'`
    echo "$NOW Available RAM: $AVAILABLE"

    CACHED=`cat /proc/meminfo | grep "Cached" | grep -v "Swap" | busybox awk '{print $2}'`
    echo "$NOW Cached RAM: $CACHED"

    FREE=`cat /proc/meminfo | grep "MemFree" | busybox awk '{print $2}'`
    echo "$NOW Free RAM: $FREE"

    DIRTY=`cat /proc/meminfo | grep "Dirty" | busybox awk '{print $2}'`
    echo "$NOW Dirty: $DIRTY"

    if [ $IOW -gt 300 ]
    then
        ps -t > /data/quasar/stacks
        head -n999 /proc/*/task/*/stack >> /data/quasar/stacks
        pkill -3 "quasar.app"
        pkill -3 "services"
        echo "thread dumps collected"
        echo "rebooting..."
        reboot
    fi

    sleep 5

done
