#!/system/bin/sh

cd /data/quasar
rm -rf daemons_logs # Due to moving logs to /tmp partition: QUASAR-8620
rm -f /data/quasar/_* # Due to TransactionFile bug: SK-4906

export CA_CERTIFICATES=/system/vendor/quasar/ca-certificates.crt
export LD_LIBRARY_PATH=/system/vendor/quasar/libs
export SQLITE_TMPDIR=/tmp	# SQLite vacuum temporary files should be written to tmpfs, otherwise eMMC lifetime will decrease dramatically

ulimit -n 10240

# This section was originally residing in the source code of the quasar_launcher, but is not present in quasar_launcher2
# Make sure oom killer does not spare yiod or other daemons if needed
#
# The story is process inherits it's oom score from parent process. Most of time this is ok.
# Killing pid 1 is strictly prohibited. The logic is pid 1 is as important as kernel and should have no bugs.
# So it has oom_score_adj of -1000 which basically means "do not kill"
#
# in Yandex Station for some reason oom_score_adj of -1000 is inherited by launcher and all daemons. This is wrong,
# becuse they are perfectly ok to die to oom killer.

echo "0" > /proc/self/oom_score_adj

nohup /system/vendor/quasar/quasar_launcher2 /system/vendor/quasar/launch.json > launcher2.out 2>&1 &

sleep 1
