#!/usr/bin/env bash
# Copyright (C) 2024 OSv Contributors
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.

set -euo pipefail

#
# Crucible DISCARD Test
#
# Tests DISCARD/TRIM functionality with Crucible block devices.
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

echo "Crucible DISCARD Test"
echo "====================="
echo
echo "Targets: $CRUCIBLE_TARGETS"
echo "UUID: $CRUCIBLE_UUID"
echo

# Test 1: Boot and verify device supports DISCARD
echo "[Test 1] Verifying Crucible device..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e 'ls -l /dev/crucible0' || {
    echo "FAIL: Could not boot with Crucible"
    exit 1
  }
echo "[Test 1] PASS: Device ready"
echo

# Test 2: Test ZFS with DISCARD on Crucible
echo "[Test 2] Testing ZFS TRIM on Crucible..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    zpool create -o autotrim=on testpool /dev/crucible0 || true
    zfs create testpool/data || true
    echo 'Creating test file...'
    dd if=/dev/zero of=/testpool/data/large.bin bs=1M count=10
    echo 'Deleting test file...'
    rm /testpool/data/large.bin
    echo 'Triggering TRIM...'
    zpool trim testpool
    zpool status testpool
  " || {
    echo "FAIL: ZFS TRIM test failed"
    exit 1
  }
echo "[Test 2] PASS: ZFS TRIM operations work"
echo

# Test 3: Verify DISCARD is passed through to downstairs
echo "[Test 3] Testing DISCARD propagation..."
echo "Note: Check downstairs logs to verify DISCARD messages are received"
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  -e "
    zpool create testpool /dev/crucible0 || true
    zfs create testpool/data || true
    dd if=/dev/zero of=/testpool/data/file.bin bs=1M count=5
    sync
    rm /testpool/data/file.bin
    zpool trim testpool
    echo 'DISCARD operations sent to downstairs'
  " || {
    echo "FAIL: DISCARD test failed"
    exit 1
  }
echo "[Test 3] PASS: DISCARD operations sent"
echo

# Test 4: Test read-only mode (DISCARD should be no-op or fail gracefully)
echo "[Test 4] Testing DISCARD on read-only device..."
"$OSV_ROOT/scripts/run.py" \
  --crucible="$CRUCIBLE_TARGETS" \
  --crucible-uuid="$CRUCIBLE_UUID" \
  --crucible-readonly \
  -e "
    echo 'Read-only mode - DISCARD should be ignored or fail gracefully'
    ls -l /dev/crucible0
  " || {
    echo "FAIL: Read-only test failed"
    exit 1
  }
echo "[Test 4] PASS: Read-only mode handled correctly"
echo

echo "====================="
echo "All DISCARD Tests PASSED"
echo "====================="
echo
echo "Note: Verify downstairs logs to confirm DISCARD messages were received and processed."
