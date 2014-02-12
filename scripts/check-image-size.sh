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

if [ $(ls -l "$1" | cut -d " " -f 5) -ge "$2" ]; then
    echo "$1 is greater than $2, needed to uncompress kernel"
    echo "Change arch/x64/boot16.S and arch/x65/lzloader.ld "
    echo " in order to have more available space for the kernel. "

    exit 1
else
    exit 0
fi
