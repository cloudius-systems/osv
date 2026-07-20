<!-- Copyright (C) 2026 Greg Burd -->
# OpenZFS-on-OSv: debugging findings

Hands-on debugging on EC2 m5d.metal, fedora:39 build container, KVM guests.

## Bug 1 - zpool export/import lfmutex owner assertion (FIXED)

Symptom:

    Assertion failed: owner.load(...) == sched::thread::current()
    (core/lfmutex.cc: unlock: 221)
    ... condvar::wait() <- libtpool tpool_worker (thread_pool.c:151)

Intermittent; reproduced deterministically (8/8) on `zpool import`.

Root cause: the OpenZFS userland thread pool (lib/libtpool) hands jobs to
worker pthreads that block in pthread_cond_wait() on a shared tp_mutex.  On
OSv a pthread mutex + condvar ARE the kernel lockfree::mutex + condvar,
whose wait-morphing protocol transfers mutex ownership from the signalling
thread to a waiter.  During tpool_destroy() teardown that handoff races: a
worker returns from pthread_cond_wait() and re-enters it, unlocking a
tp_mutex it no longer owns -> lfmutex owner assert.  The import device-scan
(zutil_import.c) and mount (libzfs_mount.c) pools default to hundreds of
workers (mount_tp_nthr = 512), which is why import triggers it reliably.

Fix (patches 0021, 0022):
  - 0021: run libtpool jobs synchronously (inline) on OSv - removes worker
    pthreads and the teardown race entirely.
  - 0022: force serial dataset mounting on OSv (belt-and-suspenders; the
    serial path already exists, gated on nthr<=1 / ZFS_SERIAL_MOUNT).
OSv threads are cheap and these jobs are short, so serial execution costs
no meaningful throughput.

Verified: 0/8 asserts (was 8/8) across create/export/import/status cycles
from a clean patch-series rebuild.

## Page-allocator assert under scrub+multi-mount (memory sizing, NOT a bug)

Symptom (intermittent, only at 2 GB guest RAM under a heavy scrub +
repeated export/import sequence):

    Assertion failed: !node_algorithms::inited(...)
    (boost/intrusive/list.hpp: iterator_to: 1310)
    ... memory::page_range_allocator::alloc <- page_pool::l2::refill

Investigation: reran the identical heavy sequence at 3G/4G/8G RAM ->
0 asserts.  It is memory pressure at the 2 GB edge, not a logic bug in the
OSv page allocator or ZFS.  OpenZFS\x27s ARC is hungrier than the old BSD-ZFS
port.  Recommendation: size ZFS runtime guests >= 4 GB (the image-populate
builder at -m 512 is fine because populate does no scrub).  No code change.

## Bug 2 - partition .0 vs .1 naming (see patch 0014 update)

OSv read_partition_table() names MBR slots 0-based: first slot is
/dev/vblkN.0.  A raw disk with no partition table stays /dev/vblkN with no
child node.  OpenZFS zfs_append_partition() appended ".1", so
`zpool create test /dev/vblk1` (raw disk) looked for /dev/vblk1.1 -> fail.
Fix: only append ".1" when that partition node actually exists; otherwise
use the whole raw disk as-is (matches Linux/FreeBSD whole-disk behavior).

## Phase D feature matrix

(populated below as features are exercised)

### Tier 0 (must work)

| Feature | Result |
|---|---|
| pool create / status / destroy | worked (status/destroy); zpool list crashed on NULL column header -> fixed-by patch 0025 |
| dataset create/destroy/mount | worked |
| file write/read/sync + verify | worked (zfsio.so byte-verify, up to 64 MiB) |
| scrub integrity | worked (0 errors on clean data) |
| props recordsize/relatime/canmount | worked (set + get reflect) |
| prop readonly | was NOT enforced (zfs_is_readonly hardcoded false) -> fixed-by patch 0026 (write vnops now EROFS; verified round-trip) |
| compression off + lz4 | worked |
| checksum fletcher4 + sha256 | worked (scrub clean) |

### Tier 1

| Feature | Result |
|---|---|
| mirror (2) | worked (create/io/scrub) |
| raidz1 (3) | worked |
| raidz2 (5) | worked |
| raidz3 (7) | worked |
| offline / online | worked (DEGRADED then ONLINE) |
| replace + resilver | worked (resilver ran to completion) |
| SLOG + L2ARC | worked (log + cache vdevs online, io ok) |
| scrub repairs corruption | worked (corrupted a mirror leg on the host; scrub found CKSUM errors and self-healed 44.5M from the good leg, 0 residual errors) |

Test harness: zfs_builder.elf booted with --nomount --noinit --preload-zfs-library
/zfsinit.so (inits /dev/zfs + /etc/mnttab), then /zpool.so + /zfs.so command
sequences against raw virtio-blk disks (vblkN).  File I/O + byte-verify via a
small /zfsio.so helper.  Corruption-repair by dd over a raw disk file between
export and re-import.

### Tier 3 (OpenZFS-only)

| Feature | Result |
|---|---|
| compression zstd | worked (write+verify, scrub clean) |
| compression gzip | worked |
| compression zle | worked |
| compression lzjb | worked |
| checksum sha512 | worked |
| checksum skein | worked |
| checksum edonr | worked |
| checksum blake3 | worked |
| encryption aes-256-gcm | worked (create keyformat=hex, write+verify, unmount/unload-key -> keystatus unavailable, load-key/mount -> write+verify again) |
| large_blocks (recordsize=1M) | worked (8 MiB file write+verify at 1M recordsize) |
| large_dnode (dnodesize=auto) | worked |
| dedup | worked (two identical 8 MiB files -> 8.1 MiB used, second copy deduplicated) |
| device_removal (zpool remove) | worked (device removed, mappings retained, data intact) |
| checkpoint | worked (zpool checkpoint create + discard) |
| draid (draid1:2d:4c:0s) | worked (ONLINE, io+scrub) |
| TRIM (zpool trim / autotrim) | fixed-by patch 0027 (vdev_disk_open now sets vdev_has_trim; trim completes 100% on virtio-blk with discard, and stays ONLINE/ENOTSUP-graceful without) |
| O_DIRECT (direct=always) | worked out of box (spl_uio.c OSv single-address-space adaptation treats page-aligned VAs as dio pages; O_DIRECT aligned write/read verified, buffered re-read consistent) |
| block_cloning | feature enabled and on-disk machinery present, BUT clone-on-copy is NOT wired: OSv copy_file_range() delegates to sendfile() (a byte copy) and there is no FICLONE ioctl / vop_copy_file_range reaching zfs_clone_range(), so copies do not share blocks.  Needs a vop_copy_file_range -> zfs_clone_range bridge. |
| raidz_expansion (zpool attach to raidz) | needs work: attach of an EMPTY raidz completes; attach with existing data starts the reflow and then STALLS at native speed (zpool status/get on the pool blocks on the dsl_pool config rrwlock held during reflow).  The run COMPLETES when slowed under gdb, i.e. it is a timing-sensitive lost-wakeup on vre->vre_cv (raidz_reflow_write_done cv_signal vs the zthr cv_wait throttle loop in vdev_raidz.c) - same class as Bug 1.  Not root-caused to a committed fix within budget. |
