# TRIM/DISCARD and Multiqueue I/O Tests

## DISCARD/TRIM Testing

### Basic Test

```bash
./scripts/run.py -e '/tests/test-discard.sh'
```

### Manual Test

```bash
./scripts/run.py -e "
mkfs.ext4 -F /dev/vblk0 && \
mount /dev/vblk0 /mnt && \
dd if=/dev/zero of=/mnt/file bs=1M count=100 && \
sync && rm /mnt/file && sync && \
fstrim -v /mnt && \
umount /mnt
"
```

### QEMU Configuration for DISCARD

Ensure QEMU is started with discard support:

```bash
./scripts/run.py --execute=/tests/test-discard.sh \
  --qemu-options="-drive file=disk.img,if=virtio,discard=unmap"
```

## Multiqueue I/O Testing

### Basic Test

```bash
./scripts/run.py -e '/tests/test-multiqueue.sh'
```

### QEMU Configuration for Multiqueue

Run with multiple queues (match SMP count):

```bash
./scripts/run.py --execute=/tests/test-multiqueue.sh \
  --smp=4 \
  --qemu-options="-device virtio-blk-pci,drive=drive0,num-queues=4 \
                  -drive file=disk.img,if=none,id=drive0"
```

### Performance Comparison

**Single Queue:**
```bash
./scripts/run.py --smp=4 -e "
dd if=/dev/zero of=/dev/vblk0 bs=1M count=1024 oflag=direct
"
```

**Multi-Queue (4 queues, parallel I/O):**
```bash
./scripts/run.py --smp=4 \
  --qemu-options="-device virtio-blk-pci,drive=drive0,num-queues=4" -e "
for i in {0..3}; do
  dd if=/dev/zero of=/dev/vblk0 bs=1M count=256 skip=\$((i*256)) oflag=direct &
done
wait
"
```

## Combined Test

Test both features together:

```bash
./scripts/run.py --smp=4 \
  --qemu-options="-device virtio-blk-pci,drive=drive0,num-queues=4,discard=unmap \
                  -drive file=disk.img,if=none,id=drive0,format=qcow2" -e "
echo 'Testing DISCARD...' && \
/tests/test-discard.sh && \
echo && \
echo 'Testing Multiqueue...' && \
/tests/test-multiqueue.sh
"
```

## Verifying Features

### Check DISCARD Support

```bash
# Check if device supports discard
cat /sys/block/vblk0/queue/discard_granularity
cat /sys/block/vblk0/queue/discard_max_bytes

# If non-zero, DISCARD is supported
```

### Check Multiqueue Support

```bash
# Number of hardware queues
cat /sys/block/vblk0/queue/nr_hw_queues

# Queue information
ls -la /sys/block/vblk0/mq/

# Per-queue CPU affinity
for q in /sys/block/vblk0/mq/*; do
  echo "Queue $(basename $q): $(cat $q/cpu_list)"
done
```

## Expected Results

### DISCARD/TRIM

- fstrim should complete without errors
- Space should be reclaimed (visible with `df` in some storage backends)
- No performance degradation

### Multiqueue

- With 4 queues and 4 vCPUs: **20-30% improvement** in parallel workloads
- Single-threaded: Similar or slightly higher latency
- Queue distribution: Even load across queues

## Troubleshooting

### DISCARD Not Working

1. Check QEMU options include `discard=unmap`
2. Verify kernel support: `dmesg | grep -i discard`
3. Check device capabilities: `/sys/block/vblk0/queue/discard_*`

### Multiqueue Not Active

1. Verify QEMU has `num-queues=N` parameter
2. Check SMP configuration: `nproc`
3. Verify queues created: `ls /sys/block/vblk0/mq/`
4. Check dmesg for multiqueue initialization

### Performance Not Improving

1. Ensure truly parallel workload (multiple concurrent I/O)
2. Match queue count to vCPU count
3. Use direct I/O (`oflag=direct`) to bypass cache
4. Check CPU utilization during test
