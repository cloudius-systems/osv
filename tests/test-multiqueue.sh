#!/bin/bash
# Test script for multiqueue I/O functionality

set -e

echo "Testing Multiqueue I/O support..."

# Check multiqueue configuration
echo "1. Checking multiqueue configuration..."
if [ -d /sys/block/vblk0/mq ]; then
    echo "   Multiqueue is enabled"
    NUM_QUEUES=$(ls -d /sys/block/vblk0/mq/* 2>/dev/null | wc -l)
    echo "   Number of hardware queues: $NUM_QUEUES"

    for queue in /sys/block/vblk0/mq/*; do
        echo "   Queue $(basename $queue):"
        if [ -f "$queue/cpu_list" ]; then
            echo "     CPU list: $(cat $queue/cpu_list)"
        fi
    done
else
    echo "   Multiqueue not available on this device"
fi

echo ""
echo "2. Running single-threaded I/O test..."
time dd if=/dev/zero of=/dev/vblk0 bs=1M count=512 oflag=direct 2>&1 | grep -E "(copied|MB/s)"

echo ""
echo "3. Running parallel I/O test (4 streams)..."
time (
    for i in {0..3}; do
        dd if=/dev/zero of=/dev/vblk0 bs=1M count=128 skip=$((i*128)) oflag=direct 2>&1 &
    done
    wait
) 2>&1 | grep -E "(real|user|sys)"

echo ""
echo "Multiqueue test completed!"
echo ""
echo "Expected results:"
echo "  - With multiqueue: 20-30% improvement in parallel workloads"
echo "  - Without multiqueue: Similar performance in both tests"
