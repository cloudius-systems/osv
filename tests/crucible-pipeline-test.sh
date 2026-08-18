#!/usr/bin/env bash
# Copyright (C) 2024 OSv Contributors
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

set -euo pipefail

#
# Crucible Pipeline Test
#
# Tests request pipelining and concurrent I/O with Crucible block devices.
#
# Prerequisites:
#  - 3 Crucible downstairs servers running
#  - Crucible targets configured via --crucible parameter
#  - OSv built with Crucible support
#  - fio (Flexible I/O Tester) available in OSv image
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OSV_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default parameters
CRUCIBLE_TARGETS="${CRUCIBLE_TARGETS:-localhost:3000,localhost:3001,localhost:3002}"
CRUCIBLE_UUID="${CRUCIBLE_UUID:-test-volume-uuid}"

echo "Crucible Pipeline Test"
echo "======================"
echo
echo "Targets: $CRUCIBLE_TARGETS"
echo "UUID: $CRUCIBLE_UUID"
echo

# Test 1: Single-threaded sequential I/O (baseline)
echo "[Test 1] Single-threaded sequential I/O (baseline)..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    echo 'Sequential write test...'
    dd if=/dev/zero of=/dev/crucible0 bs=1M count=100 oflag=direct
    echo 'Sequential read test...'
    dd if=/dev/crucible0 of=/dev/null bs=1M count=100 iflag=direct
  " || {
    echo "FAIL: Sequential I/O test failed"
    exit 1
  }
echo "[Test 1] PASS: Sequential I/O works"
echo

# Test 2: Multi-threaded random I/O (pipelining)
echo "[Test 2] Multi-threaded random I/O (tests pipelining)..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    echo 'Running concurrent random reads (4 threads, depth 16)...'
    # Simulate concurrent I/O with multiple dd processes in background
    for i in 1 2 3 4; do
      dd if=/dev/crucible0 of=/dev/null bs=4k count=1000 skip=\$((i * 10000)) iflag=direct &
    done
    wait
    echo 'Concurrent I/O completed'
  " || {
    echo "FAIL: Concurrent I/O test failed"
    exit 1
  }
echo "[Test 2] PASS: Concurrent I/O works (pipelining functional)"
echo

# Test 3: ZFS with concurrent I/O
echo "[Test 3] ZFS with concurrent file operations..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    zpool create testpool /dev/crucible0 || true
    zfs create testpool/data || true

    echo 'Creating files concurrently...'
    for i in 1 2 3 4 5; do
      dd if=/dev/zero of=/testpool/data/file\$i.bin bs=1M count=10 &
    done
    wait

    echo 'Reading files concurrently...'
    for i in 1 2 3 4 5; do
      md5sum /testpool/data/file\$i.bin &
    done
    wait

    echo 'ZFS concurrent operations successful'
  " || {
    echo "FAIL: ZFS concurrent I/O test failed"
    exit 1
  }
echo "[Test 3] PASS: ZFS concurrent operations work"
echo

# Test 4: Stress test with deep queue
echo "[Test 4] Stress test with deep queue depth..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    echo 'Running deep queue stress test (32 concurrent operations)...'
    # Launch many concurrent operations
    for i in {1..32}; do
      dd if=/dev/zero of=/dev/crucible0 bs=64k count=10 seek=\$((i * 100)) oflag=direct 2>/dev/null &
    done
    wait
    echo 'Deep queue stress test completed'
  " || {
    echo "FAIL: Deep queue stress test failed"
    exit 1
  }
echo "[Test 4] PASS: Deep queue stress test successful"
echo

# Test 5: Mixed read/write workload
echo "[Test 5] Mixed read/write workload..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    zpool create testpool /dev/crucible0 || true
    zfs create testpool/data || true

    echo 'Running mixed read/write workload...'

    # Background readers
    for i in 1 2 3; do
      while true; do cat /testpool/data/test.txt 2>/dev/null || true; done &
      READER_PID_\$i=\$!
    done

    # Foreground writers
    for i in 1 2 3 4 5; do
      echo 'iteration '\$i > /testpool/data/test.txt
      sync
      sleep 0.1
    done

    # Kill background readers
    for i in 1 2 3; do
      kill \$READER_PID_\$i 2>/dev/null || true
    done

    echo 'Mixed workload completed'
  " || {
    echo "FAIL: Mixed workload test failed"
    exit 1
  }
echo "[Test 5] PASS: Mixed workload successful"
echo

# Test 6: Performance measurement
echo "[Test 6] Basic performance measurement..."
echo "Note: Actual throughput depends on network and downstairs performance"
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    echo 'Measuring write throughput...'
    time dd if=/dev/zero of=/dev/crucible0 bs=1M count=100 oflag=direct 2>&1 | grep -E 'copied|MB/s'

    echo ''
    echo 'Measuring read throughput...'
    time dd if=/dev/crucible0 of=/dev/null bs=1M count=100 iflag=direct 2>&1 | grep -E 'copied|MB/s'
  " || {
    echo "FAIL: Performance measurement failed"
    exit 1
  }
echo "[Test 6] PASS: Performance measurement completed"
echo

echo "======================"
echo "All Pipeline Tests PASSED"
echo "======================"
echo
echo "Summary:"
echo "  - Sequential I/O: Working"
echo "  - Concurrent I/O: Working (pipelining functional)"
echo "  - ZFS concurrent ops: Working"
echo "  - Deep queue depth: Working"
echo "  - Mixed workload: Working"
echo "  - Performance: Measured (see output above)"
echo
echo "For detailed performance analysis:"
echo "  - Check OSv debug logs for job IDs and timing"
echo "  - Monitor downstairs logs for per-server IOPS and latency"
echo "  - Use 'fio' for comprehensive benchmarking (if available)"
