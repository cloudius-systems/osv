#!/bin/bash

# This script is a work in progress. It is meant to help run
# test programs named "misc-*.*c" found under ./tests directory.
# Some of the programs need to be run with specific number of CPUs
# or enough of RAM or enough of disk (10G). They also sometimes take some
# arguments to drive their behavior.
# This script acts also as some sort of inventory of those tests.
# We hope that possibly some of these tests or at least portion of these may
# become unit tests as well.
# Finally, please note that some of the tests are not enabled as they require
# extra special setup (like extra disk) which we hope to automate at some point.

run_test()
{
  local COMMAND="misc-$1"
  local RUN_OPTIONS=$2

  echo "------------------------------------------------------------------------------"
  echo "Running ${COMMAND}"
  echo "------------------------------------------------------------------------------"
  ./scripts/run.py -e "--power-off-on-abort /tests/${COMMAND}" $RUN_OPTIONS
  echo ""
}

test_block_device()
{
  echo "Tests disabled as they require separate test disk"
#TODO: Requires setting up separate disk
#run_test bdev-rw
#run_test bdev-wlatency
#run_test bdev-write
}

test_io()
{
  run_test "concurrent-io.so setup"
  run_test "concurrent-io.so read-diff-ranges"
}

test_scheduler()
{
  run_test ctxsw.so
  run_test loadbalance.so "-c 2"
  run_test scheduler.so "-c 1"
  run_test setpriority.so
  run_test timeslice.so "-c 1"
#TODO: run_test wake.so Most of it is commented out
}

test_memory()
{
  run_test free-perf.so
  #run_test leak.so - make sense to run only with trace.py
  run_test malloc.so "-m 4G"
  run_test memcpy.so
  run_test mmap-anon-perf.so
  run_test mmap-big-file.so #Takes LONG time to run
}

test_locking()
{
  run_test "futex-perf.so 1 500 1" "-c 1"
  run_test lfring.so
  run_test lock-perf.so
  run_test "mutex2.so 2 500"
  run_test mutex.so
}

test_others()
{
  run_test bsd-callout.so
#TODO: run_test console.so
#TODO: run_test execve-payload
#TODO: run_test execve
  run_test fs-stress.so
#TODO: run_test fsx.so - hangs
  run_test gtod.so
  run_test printf.so
  run_test procfs.so
  run_test random.so
#TODO: run_test readbench.so - does not work due to missing file
  run_test tls.so
  run_test urandom.so
}

test_tcp()
{
  run_test sockets.so
  run_test tcp-hash-srv.so
  run_test tcp-sendonly.so
#TODO: run_test tcp.so
}

test_zfs()
{
  run_test zfs-arc.so
  run_test zfs-io.so
}

TEST_NAME=$1
case "$TEST_NAME" in
  --help|-h)
    echo "Usage: $0 <test_family> | <test_command>"
    exit 1;;
  block_device)
    test_block_device;;
  io)
    test_io;;
  scheduler)
    test_scheduler;;
  memory)
    test_memory;;
  locking)
    test_locking;;
  others)
    test_others;;
  tcp)
    test_tcp;;
  zfs)
    test_zfs;;
  *)
    run_test "$@"
esac
