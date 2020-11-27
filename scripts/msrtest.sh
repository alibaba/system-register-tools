#!/usr/bin/env bash

# Mainly test core0
i=1
interval=1
cpu_num=`cat /proc/stat | grep cpu[0-9] -c`

while [ $i -le 500 ]
do
	taskset -c 0 rdasr -p0 -r MIDR_EL1 &
	taskset -c 0 rdasr -p1 -r MPIDR_EL1 &
	taskset -c 0 rdasr -p1 -r MPIDR_EL1 &
	taskset -c 1 rdasr -p1 -r MIDR_EL1 &
	taskset -c 0 rdasr -p1 -r MPIDR_EL1 &
	taskset -c 1 rdasr -p2 -r MIDR_EL1 &
	taskset -c 0 rdasr -p1 -r MPIDR_EL1 &
	taskset -c 0 rdasr -p1 -r MIDR_EL1 &
	let i++
done

wait
echo "End of test!"
