# Block Device DISCARD/TRIM Support

## Overview

OSv now supports DISCARD (also known as TRIM) operations for block devices. DISCARD allows the operating system to inform the storage device about blocks that are no longer in use, enabling space reclamation and improved performance on SSDs and thin-provisioned storage.

## Architecture

### BIO Commands

The `BIO_DISCARD` command has been added to the bio subsystem (`include/osv/bio.h`):

```c
#define BIO_DISCARD  0x20  /* Space reclamation (TRIM) */
```

### VirtIO Block Device

The virtio-blk driver supports DISCARD through the `VIRTIO_BLK_F_DISCARD` feature flag. When negotiated with the host, the driver can send `VIRTIO_BLK_T_DISCARD` requests to reclaim space.

#### Configuration

The driver reads the following DISCARD-related configuration from the device:
- `max_discard_sectors`: Maximum number of sectors per DISCARD request
- `max_discard_seg`: Maximum number of DISCARD segments
- `discard_sector_alignment`: Required alignment for DISCARD operations

### IOCTL Interface

The `BLKDISCARD` ioctl allows userspace to issue DISCARD commands:

```c
#define BLKDISCARD _IO(0x12, 119)
```

Usage:
```c
uint64_t range[2];
range[0] = start_offset;  // in bytes
range[1] = length;        // in bytes
ioctl(fd, BLKDISCARD, range);
```

## Filesystem Support

### fstrim

The `fstrim` utility can be used to discard unused blocks on mounted filesystems:

```bash
# Trim all unused blocks on /mnt
fstrim -v /mnt

# Trim with minimum extent size
fstrim -v -m 1048576 /mnt  # Only trim extents >= 1MB
```

### Supported Filesystems

- **ext4**: Full DISCARD support (mount with `-o discard` for automatic TRIM or use fstrim)
- **XFS**: Full DISCARD support
- **ZFS**: DISCARD support for space reclamation
- **Btrfs**: Full DISCARD support

## Performance Considerations

### Automatic vs. Manual TRIM

**Automatic TRIM** (mount option `-o discard`):
- Pros: Immediate space reclamation
- Cons: Performance overhead on every delete operation

**Manual TRIM** (periodic fstrim):
- Pros: Lower overhead, batched operations
- Cons: Delayed space reclamation

Recommendation: Use periodic fstrim (e.g., daily/weekly) rather than automatic TRIM for better performance.

### DISCARD Alignment

For optimal performance, DISCARD operations should be aligned to the device's discard alignment boundary. The virtio-blk driver automatically handles this based on device capabilities.

## Testing

### Basic DISCARD Test

```bash
# Create a filesystem
mkfs.ext4 /dev/vblk0

# Mount it
mount /dev/vblk0 /mnt

# Create and delete a large file
dd if=/dev/zero of=/mnt/testfile bs=1M count=100
sync
rm /mnt/testfile
sync

# Trim the filesystem
fstrim -v /mnt
```

### Verify DISCARD Support

Check if the device supports DISCARD:
```bash
cat /sys/block/vblk0/queue/discard_granularity
cat /sys/block/vblk0/queue/discard_max_bytes
```

## Implementation Details

### Request Flow

1. Userspace calls `ioctl(fd, BLKDISCARD, range)`
2. Kernel creates a `bio` with `bio_cmd = BIO_DISCARD`
3. Driver checks `VIRTIO_BLK_F_DISCARD` feature
4. Driver builds a `blk_discard_write_zeroes` descriptor
5. Request is submitted to virtio queue
6. Device processes DISCARD and reclaims space

### Error Handling

- `EOPNOTSUPP`: Device does not support DISCARD
- `EINVAL`: Invalid range or alignment
- `EROFS`: Filesystem is read-only
- `EIO`: I/O error during DISCARD operation

## QEMU Configuration

To enable DISCARD support in QEMU:

```bash
qemu-system-x86_64 \
  -drive file=disk.img,if=virtio,discard=unmap \
  ...
```

For raw files (hole punching):
```bash
qemu-system-x86_64 \
  -drive file=disk.img,if=virtio,discard=unmap,detect-zeroes=unmap \
  ...
```

## Limitations

- DISCARD is a hint to the device; there's no guarantee space will be reclaimed
- Some storage backends (e.g., non-sparse files) may ignore DISCARD
- DISCARD operations are not atomic
- Large DISCARD operations may take time and impact performance

## References

- [VirtIO Block Device Specification](https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-2480005)
- [Linux TRIM/DISCARD Documentation](https://www.kernel.org/doc/Documentation/block/discard.txt)
