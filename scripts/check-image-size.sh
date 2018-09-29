#!/bin/bash
#
# Copyright (C) 2014 Eduardo Piva
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
if [ "$#" -ne 1 ]; then
    echo "usage: $0 file"
    exit 1
fi

size=$(ls -l "$1" | cut -d " " -f 5)

if [ $size -ge 4294967295 ]; then
    echo "$1 is bigger than ULONG_MAX limit of uncompressing kernel."
    echo "If you really need such a huge kernel change offsets to use 64-bit long long int numbers."
    exit 1
fi
exit 0
