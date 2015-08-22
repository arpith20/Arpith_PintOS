#!/bin/bash
cd ./src/threads/
echo "cleaning..."
make clean
echo "building"
make
echo "running alarm-signal"
cd build
pintos --qemu -- -q run alarm-signal
START=$SECONDS
make check
END=$SECONDS
TIME=`expr $END - $START`
echo "it took "$TIME "seconds to run the tests"