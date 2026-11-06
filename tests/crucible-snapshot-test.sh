#!/usr/bin/env bash
# Copyright (C) 2024 OSv Contributors
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

set -euo pipefail

#
# Crucible Snapshot Test
#
# Tests snapshot creation functionality with Crucible block devices.
#
# Prerequisites:
#  - 3 Crucible downstairs servers running
#  - Crucible targets configured via --crucible parameter
#  - OSv built with Crucible support
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OSV_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default parameters
CRUCIBLE_TARGETS="${CRUCIBLE_TARGETS:-localhost:3000,localhost:3001,localhost:3002}"
CRUCIBLE_UUID="${CRUCIBLE_UUID:-test-volume-uuid}"

echo "Crucible Snapshot Test"
echo "======================"
echo
echo "Targets: $CRUCIBLE_TARGETS"
echo "UUID: $CRUCIBLE_UUID"
echo

# Test 1: Boot with Crucible and verify device
echo "[Test 1] Booting OSv with Crucible..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e 'ls -l /dev/crucible0' || {
    echo "FAIL: Could not boot with Crucible"
    exit 1
  }
echo "[Test 1] PASS: Device created"
echo

# Test 2: Create snapshot with ID 1
echo "[Test 2] Creating snapshot with ID 1..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e 'crucible-snapshot /dev/crucible0 1' || {
    echo "FAIL: Could not create snapshot"
    exit 1
  }
echo "[Test 2] PASS: Snapshot 1 created"
echo

# Test 3: Write data, create snapshot, verify
echo "[Test 3] Testing data persistence across snapshot..."
SNAPSHOT_ID=$(date +%s)
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    echo 'test data' > /tmp/test.txt
    cat /tmp/test.txt
    crucible-snapshot /dev/crucible0 $SNAPSHOT_ID
    echo 'Snapshot $SNAPSHOT_ID created'
  " || {
    echo "FAIL: Data write and snapshot failed"
    exit 1
  }
echo "[Test 3] PASS: Data and snapshot operations successful"
echo

# Test 4: Test snapshot with ZFS
echo "[Test 4] Testing snapshot with ZFS pool..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    zpool create testpool /dev/crucible0 || true
    zfs create testpool/data || true
    echo 'test' > /testpool/data/file.txt
    crucible-snapshot /dev/crucible0 99999
    cat /testpool/data/file.txt
  " || {
    echo "FAIL: ZFS with snapshot failed"
    exit 1
  }
echo "[Test 4] PASS: ZFS and Crucible snapshot integration works"
echo

# Test 5: Test read-only mode (snapshot should fail)
echo "[Test 5] Testing snapshot on read-only device (should fail)..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  --crucible-readonly \
  -e 'crucible-snapshot /dev/crucible0 88888' && {
    echo "FAIL: Snapshot should fail on read-only device"
    exit 1
  } || {
    echo "[Test 5] PASS: Snapshot correctly rejected on read-only device"
  }
echo

echo "======================"
echo "All Snapshot Tests PASSED"
echo "======================"
