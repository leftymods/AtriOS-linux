#!/system/bin/sh

# Script is intended to periodically dump general VM information
# from kernel debug filesystems.

LOG_DIR="/data/quasar/mem_monitor"

VMSTAT_OUT=$LOG_DIR/vmstat.log
EXTFRAG_OUT=$LOG_DIR/extfrag.log
ION_OUT=$LOG_DIR/ion.log
SYSFS_GPU_OUT=$LOG_DIR/sysfs_gpu.log
DMESG_OUT=$LOG_DIR/dmesg.log

##########

echo ">>> Deleting previous content of $LOG_DIR..."
rm -rf $LOG_DIR

echo ">>> Creating $LOG_DIR..."
mkdir -p $LOG_DIR

##########

# Moves logs from main directory into LOG_DIR_NUMBER_MAX consequently from 1.
# Each directory can contain LOG_DIR_THRESHOLD_SIZE_MB of logs
# Sum size of all logs is not greater than LOG_DIR_THRESHOLD_SIZE_MB * LOG_DIR_NUMBER_MAX

LOG_DIR_NUMBER=1
LOG_DIR_NUMBER_MAX=5
LOG_DIR_THRESHOLD_SIZE_MB=30

backup_logs ()
{

	LOG_DIR_SIZE=$(du -sm $LOG_DIR | cut -f1)

	# If logging directory is very large - move it to the backup logging
	# directory with LOG_DIR_NUMBER prefix

	if [ $LOG_DIR_SIZE -ge $LOG_DIR_THRESHOLD_SIZE_MB ]; then

		# Remove old backup directory and create it again

		rm -rf $LOG_DIR.$LOG_DIR_NUMBER
		mkdir -p $LOG_DIR.$LOG_DIR_NUMBER

		# Move current logs into new backup directory

		echo ">>> Move logs into $LOG_DIR.$LOG_DIR_NUMBER directory"
		mv $LOG_DIR/* $LOG_DIR.$LOG_DIR_NUMBER/

		# Increment backup directory counter - next directory to backup
		# will be either LOG_DIR_NUMBER += 1, or reset to 1

		if [ $LOG_DIR_NUMBER -ge $LOG_DIR_NUMBER_MAX ]; then
			LOG_DIR_NUMBER=1
		else
			let LOG_DIR_NUMBER=LOG_DIR_NUMBER+1
		fi
	fi
}

print_timestamp ()
{
	echo "\n\t\t\t\t\t[$(date)]: \n" >> $VMSTAT_OUT
	echo "\n\t\t\t\t\t[$(date)]: \n" >> $EXTFRAG_OUT
	echo "\n\t\t\t\t\t[$(date)]: \n" >> $ION_OUT
	echo "\n\t\t\t\t\t[$(date)]: \n" >> $SYSFS_GPU_OUT
	echo "\n\t\t\t\t\t[$(date)]: \n" >> $DMESG_OUT
}

vmstat_dump ()
{
	echo "\n>>> VMSTAT DUMP: " >> $VMSTAT_OUT
	cat /proc/vmstat >> $VMSTAT_OUT
}

extfrag_dump ()
{
	echo "\n>>> EXTFRAG DUMP: " >> $EXTFRAG_OUT
	cat /d/extfrag/extfrag_index >> $EXTFRAG_OUT
	cat /d/extfrag/unusable_index >> $EXTFRAG_OUT
}

ion_dump ()
{
	echo "\n>>> ION DUMP: " >> $ION_OUT
	echo "CMA heap: " >> $ION_OUT
	cat /d/ion/heaps/cma >> $ION_OUT

	echo "system heap: " >> $ION_OUT
	cat /d/ion/heaps/system >> $ION_OUT
}

sysfs_gpu_dump ()
{
	echo "\n>>> SYSFS GPU DUMP: " >> $SYSFS_GPU_OUT
	cat /d/mali/gpu_memory >> $SYSFS_GPU_OUT
}

dmesg_dump ()
{
	dmesg | tail -n 30 >> $DMESG_OUT
}

echo ">>> Start data collection..."
while true
do
	print_timestamp

	vmstat_dump
	extfrag_dump
	ion_dump
	sysfs_gpu_dump
	dmesg_dump

	backup_logs

	echo -n "."
	sleep 1
done
