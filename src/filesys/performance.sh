#!/bin/bash
echo "cleaning..."
make clean
echo "building"
make
status=$?;
if [ $status -eq 0 ]
then
	echo "checking"
	cd build
	START=$SECONDS
	make check
	END=$SECONDS
	TIME=`expr $END - $START`
	echo "it took "$TIME "seconds to run the tests"
fi
