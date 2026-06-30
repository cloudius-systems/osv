# Block Device DISCARD/TRIM Support

## Overview

The virtio-blk driver supports DISCARD (TRIM) requests, letting the guest
tell the backing store which sectors are no longer in use so it can
reclaim space on SSDs and thin-provisioned/sparse images.

## BIO command

A `BIO_DISCARD` command was added to the bio layer
(`include/osv/bio.h`):

```c
#define BIO_DISCARD  0x20  /* Space reclamation (TRIM) */
```

A bio carrying `BIO_DISCARD` describes a byte range
(`bio_offset`, `bio_bcount`) to discard.

## ioctl interface

`BLKDISCARD` lets in-guest code issue a discard against an open block
device (`include/api/sys/mount.h`, handled in `drivers/blk-common.cc`):

```c
#define BLKDISCARD _IO(0x12, 119)

uint64_t range[2];
range[0] = start_offset;  // bytes
range[1] = length;        // bytes
ioctl(fd, BLKDISCARD, range);
```

`blk_ioctl()` builds a `BIO_DISCARD` bio for `[range[0], range[0]+range[1])`,
submits it to the device's `strategy` routine, and waits for completion.

## virtio-blk implementation

The driver negotiates `VIRTIO_BLK_F_DISCARD` (feature bit 13).  When the
host offers it, the driver reads the discard limits from device config
(`drivers/virtio-blk.cc`):

- `max_discard_sectors`
- `max_discard_seg`
- `discard_sector_alignment`

A `BIO_DISCARD` request is translated to a `VIRTIO_BLK_T_DISCARD` (11)
command carrying a single `blk_discard_write_zeroes` descriptor built from
the bio's sector range:

```c
req->discard_desc.sector       = bio->bio_offset / sector_size;
req->discard_desc.num_sectors  = bio->bio_bcount / sector_size;
req->discard_desc.flags        = 0;
```

If the feature was not negotiated, a `BIO_DISCARD` bio is failed rather
than silently dropped, so callers can fall back (e.g. overwrite with
zeroes).

## Request flow

1. Caller issues `ioctl(fd, BLKDISCARD, range)` (or a filesystem submits a
   `BIO_DISCARD` bio directly).
2. A bio with `bio_cmd = BIO_DISCARD` is created for the byte range.
3. The driver checks `VIRTIO_BLK_F_DISCARD`; if absent the request fails.
4. The driver emits a `VIRTIO_BLK_T_DISCARD` descriptor and submits it.
5. The device reclaims the sectors and completes the request.

## QEMU configuration

Discard must be enabled on the backing drive for the host to act on it:

```bash
qemu-system-x86_64 \
  -drive file=disk.img,if=virtio,discard=unmap \
  ...
```

For raw images, add `detect-zeroes=unmap` to punch holes.

## Limitations

- DISCARD is a hint; the backend may ignore it (e.g. non-sparse files).
- The driver emits one discard segment per request.
- DISCARD is not atomic with respect to concurrent I/O to the same range.

## References

- [VirtIO Block Device Specification](https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-2480005)
