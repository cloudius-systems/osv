#!/bin/bash

set -ev

BIN=$1
SRC_DIR=$(dirname "$BIN")/..
SRC_FILE_BASE=$(basename "$BIN" | sed 's/\.so$//')
SRC_FILE=$SRC_FILE_BASE.c

cd $SRC_DIR
touch $SRC_FILE
CMD1=$(make V=1 | grep "\-o .libs/$SRC_FILE_BASE")
CMD2=$(echo $CMD1 | sed -e "s|^libtool: link: ||" -e "s| -o .libs/$SRC_FILE_BASE | -shared -o .libs/$SRC_FILE_BASE.so |")
$CMD2
