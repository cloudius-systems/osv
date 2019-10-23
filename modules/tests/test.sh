#!/bin/bash

THIS_DIR=$(readlink -f $(dirname $0))
$THIS_DIR/../../scripts/test.py
