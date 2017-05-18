#!/bin/bash
#
# Copyright (C) 2014 Eduardo Piva
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
if [ "$#" -ne 2 ]; then
    echo "usage: $0 file maxsize"
    exit 1
fi

size=$(ls -l "$1" | cut -d " " -f 5)

if [ $size -ge 2147483647 ]; then
    echo "$1 is bigger than INT_MAX limit of uncompressing kernel."
    echo "If you really need such a huge kernel, fix fastlz/lzloader.cc"
    exit 1
fi
if [ $size -ge "$2" ]; then
    echo "$1 is bigger than $2 available for uncompressing kernel."
    echo "Increase 'lzkernel_base' in Makefile to raise this limit."
    exit 1
fi
exit 0
