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
