#!/bin/bash
# Test script for Crucible driver boot behavior
# Tests that OSv boots successfully with various Crucible configurations

set -euo pipefail

OSV_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$OSV_DIR"

echo "=== Crucible Boot Test Suite ==="
echo

# Test 1: Boot without --crucible parameter (should succeed immediately)
echo "Test 1: Boot without Crucible driver (baseline)"
echo "Expected: OSv boots normally, no Crucible messages"
# ./scripts/run.py --no-crucible
echo "SKIP: Manual test - boot OSv without --crucible parameter"
echo

# Test 2: Boot with --crucible but servers unavailable (should boot with warning)
echo "Test 2: Boot with Crucible enabled but servers unavailable"
echo "Expected: OSv boots, warning logged, /dev/crucible0 not created"
echo "Command: ./scripts/run.py --crucible=localhost:8810,localhost:8820,localhost:8830 --crucible-uuid=12345678-1234-1234-1234-123456789abc"
echo "SKIP: Manual test - boot OSv with --crucible but no downstairs servers running"
echo

# Test 3: Boot with --crucible and servers available (should succeed)
echo "Test 3: Boot with Crucible enabled and servers available"
echo "Expected: OSv boots, /dev/crucible0 created successfully"
echo "Prerequisites: Run crucible-basic-test.sh first to start downstairs servers"
echo "Command: ./scripts/run.py --crucible=localhost:8810,localhost:8820,localhost:8830 --crucible-uuid=12345678-1234-1234-1234-123456789abc"
echo "SKIP: Manual test - requires running downstairs servers"
echo

echo "=== Test Summary ==="
echo "These are manual integration tests. Run each scenario and verify:"
echo "1. OSv boots without blocking in all cases"
echo "2. Appropriate log messages are shown (SUCCESS or WARNING)"
echo "3. /dev/crucible0 only appears when connection succeeds"
echo "4. No boot loop or hang occurs when servers are unavailable"
