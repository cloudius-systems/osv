#!/bin/bash
# Test script for DISCARD/TRIM functionality

set -e

echo "Testing DISCARD/TRIM support..."

# Format and mount a test device
echo "1. Creating ext4 filesystem..."
mkfs.ext4 -F /dev/vblk0

echo "2. Mounting filesystem..."
mount /dev/vblk0 /mnt

echo "3. Creating test file..."
dd if=/dev/zero of=/mnt/testfile bs=1M count=100
sync

echo "4. Checking disk usage before deletion..."
df -h /mnt

echo "5. Deleting test file..."
rm /mnt/testfile
sync

echo "6. Running fstrim..."
fstrim -v /mnt

echo "7. Checking disk usage after fstrim..."
df -h /mnt

echo "8. Unmounting..."
umount /mnt

echo "DISCARD test completed successfully!"
