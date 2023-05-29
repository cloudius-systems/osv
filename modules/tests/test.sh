#!/bin/bash

THIS_DIR=$(readlink -f $(dirname $0))

if [ "$OSV_KERNEL" != "" ]; then
  echo "Running with kernel $OSV_KERNEL"
  if [ "$OSV_HYPERVISOR" == "firecracker" ]; then
     RUN_OPTIONS="--kernel $OSV_KERNEL"
  else
     RUN_OPTIONS="-k --kernel-path $OSV_KERNEL"
  fi
  #The image may have been by capstan which does not handle symlinks correctly (until we fix it)
  #so for now let us disable tst-fallocate.so which relies on a symlink
  $THIS_DIR/../../scripts/test.py -p $OSV_HYPERVISOR --disabled_list tracing_smoke_test --disabled_list tst-fallocate.so --run_options "$RUN_OPTIONS"
else
  $THIS_DIR/../../scripts/test.py -p $OSV_HYPERVISOR --disabled_list tracing_smoke_test --disabled_list tst-fallocate.so
fi
