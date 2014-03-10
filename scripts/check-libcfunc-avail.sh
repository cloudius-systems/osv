#!/bin/bash

if [ $# != 2 ]; then
    echo "usage: $0 [release | debug] [app.so]"
    exit 1
fi

MODE=$1
APP=$2

if [ ! -f build/$MODE/loader.elf ]; then
    echo "build/$MODE/loader.elf not found"
    exit 1
fi
DUMPFILE=`mktemp`
objdump -t build/$MODE/loader.elf > $DUMPFILE
FUNCS=`objdump -T $APP | grep GLIBC|sed -e "s/.*GLIBC_[0-9.]* //"`
for i in $FUNCS; do
    grep -q " $i$" $DUMPFILE
    FOUND=$?
    if [ $FOUND != 0 ]; then
        echo "$i not found"
    fi
done
rm $DUMPFILE
