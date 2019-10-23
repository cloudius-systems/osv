#!/bin/bash

THIS_DIR=$(readlink -f $(dirname $0))
CMDLINE=$($THIS_DIR/../../apps/cmdline.sh $THIS_DIR)

$THIS_DIR/../../scripts/tests/test_app.py -e "$CMDLINE" \
 --line "Rest API server running" \
 --input_line "ls -l /" \
 --input_line "exit"
