#!/bin/bash
cd ./src/threads/
if [ $# -eq 1 ]
	then
	make
	START=$(date +%s.%N)
	pintos --qemu -- -q run $1
	END=$(date +%s.%N)
	sleep 2
	DIFF=$(echo "$END - $START" | bc)
	echo $DIFF "seconds to run this program"
else
	echo "cleaning..."
	make clean
	echo "building"
	make
	echo "running alarm-signal"
	cd build
	pintos --qemu -- -q run alarm-single
	START=$SECONDS
	make check
	END=$SECONDS
	TIME=`expr $END - $START`
	echo "it took "$TIME "seconds to run the tests"
fi