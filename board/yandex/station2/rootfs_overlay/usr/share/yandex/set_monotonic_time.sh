#!/system/bin/sh

QUASAR_ROOT="/system/vendor/quasar"
MONOTONIC_TIME="/data/quasar/data/monotonic_time"
export LD_LIBRARY_PATH=${QUASAR_ROOT}:${QUASAR_ROOT}/libs:$LD_LIBRARY_PATH

currentTime=`date +%s`
controlTime=`${QUASAR_ROOT}/maind --build-timestamp | grep "Build Timestamp:" | sed -e "s|Build Timestamp:[ ]*\([0-9]*\)|\1|g" 2>/dev/null || echo 0`
if [ "$controlTime" -gt "0" ]; then
	# do nothing
	true
else
    controlTime=1549411200 # 06.02.2019 00:00:00 UTC
    echo "Invalid build timestamp. Use hardcode timestamp \"`date -d @$controlTime`\""
fi

if [ -s /data/quasar/data/monotonic_time ]; then
	monotonicTime=`cat "${MONOTONIC_TIME}" | head -1 | tr -d '\n'`
	if [ "$monotonicTime" -gt "$controlTime" ]; then
		echo "Use last monotonic timestamp"
		controlTime=$monotonicTime
	else
		echo "Invalid monotonic timestamp. Use timestamp \"`date -d @$controlTime`\""
	fi
fi

if [ "$currentTime" -lt "$controlTime" ]; then
	date -s @$controlTime
else
    echo "Time was not changed"
fi
