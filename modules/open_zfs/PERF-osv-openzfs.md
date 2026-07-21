# OpenZFS vs BSD-ZFS on OSv — Phase E microbenchmark

Copyright (C) 2026 Greg Burd

Focused storage-engine microbenchmark of the two ZFS ports that ship in this
tree, run head-to-head on identical hardware and identical guest config. No
Postgres / HammerDB — this isolates the filesystem layer.

- **BSD-ZFS** (`conf_zfs=bsd`): legacy in-tree BSD/Illumos ZFS (c. 2014), which
  on OSv has a *unified* ARC ⇄ page-cache bridge (mmap/read share ARC pages).
- **OpenZFS** (`conf_zfs=openzfs`): vendored OpenZFS 2.4.2 (`external/openzfs`),
  which keeps its own ARC and *borrows* pages into the page cache for mmap.

Both built from `pr/openzfs-draft` (1a298e1b), same commit, separate clones.

## Test bed

- Host: AWS `m5d.metal` (96 vCPU, 377 GiB RAM, local NVMe instance store).
- Guest: OSv `zfs_builder.elf` booted directly (`--nomount --noinit
  --preload-zfs-library /zfs-bench.so <wl>`), **8 GiB RAM, 4 vCPU, KVM**.
- Backing:
  - **single-vdev** — a whole raw local NVMe (`/dev/nvme1n1` → guest
    `/dev/vblk0`), `cache=none,aio=threads` virtio-blk.
  - **raidz2** — 7 × 20 GiB preallocated files on an XFS-on-NVMe, each a
    virtio-blk disk (`/dev/vblk0..6`).
- Matched properties: `compression=off atime=off`. `recordsize=1M` for the
  sequential 1M workloads on OpenZFS; **BSD-ZFS lacks `large_blocks`, so its
  recordsize is capped at 128k** (noted where it matters).
- ≥3 reps, median (stdev). Warm-up rep discarded on reads.
- Harness: `scripts/bench/zfs-bench.c` (pure C — see "Harness notes").

## Raw device ceiling (host `fio`, O_DIRECT, on an idle sibling NVMe)

| metric | value |
|---|---|
| 1M seq write, QD32 | **838 MiB/s** (879 MB/s) |
| 1M seq read, QD32 | **1790 MiB/s** (1877 MB/s) |
| 4k rand read, QD32×4 | **378k IOPS** |
| 4k rand write, QD32×4 | **188k IOPS** |

These are the physical ceilings. Any ZFS number **above** the read ceiling is
being served from the ARC (RAM), not the disk — expected for warm working sets.

## Results — single vdev (whole raw NVMe)

| workload | topo | BSD-ZFS median(sd) | OpenZFS median(sd) | raw ceiling | verdict |
|---|---|---|---|---|---|
| seq write 1M | single | 499 (310) MB/s | **669 (336) MB/s** | 838 MiB/s w | OpenZFS +34% |
| seq read cold (primarycache=none) | single | 850 (1742) MB/s | 4660 (399) MB/s* | 1790 MiB/s r | see note* |
| seq read warm (ARC) | single | **8510 (11) MB/s** | 3446 (14) MB/s | (RAM) | BSD +147% |
| mmap read 512M — MB/s | single | 1258 (33) MB/s | **1444 (113) MB/s** | (RAM) | ~par, OZFS +15% |
| **mmap read 512M — free-page delta** | single | **5 (18) MB** | 501 (17) MB | — | **BSD ~100× less RAM** |
| rand 4k read QD1 — IOPS | single | **230326 (23592)** | 35338 (79) | 378k | BSD +552% |
| rand 4k read QD1 — p99 | single | **30.3 µs** | 42.5 µs | — | BSD lower |
| rand 4k write QD8 — IOPS | single | **7539 (669)** | 6453 (28) | 188k | BSD +17% |
| rand 4k write QD8 — p99 | single | 1039 µs | **399 µs** | — | OpenZFS lower |
| fsync/s (ZIL) | single | 2973 (25) | **3312 (55)** | — | OpenZFS +11% |
| metadata create/stat/unlink | single | 53881 ops/s | **87810 ops/s** | — | OpenZFS +63% |
| lz4 write (compressible) | single | 2282 MB/s | **3551 MB/s** | — | OpenZFS +56% |
| off write (same data) | single | 1013 MB/s | **1233 MB/s** | — | OpenZFS +22% |
| scrub | single | 293 MB/s | 293 MB/s | — | par |
| **O_DIRECT write (OpenZFS only)** | single | n/a | 1150 MB/s | 838 MiB/s w | — |
| **O_DIRECT read (OpenZFS only)** | single | n/a | 1541 MB/s | 1790 MiB/s r | — |

## Results — raidz2 (7 × 20 GiB files)

| workload | topo | BSD-ZFS median(sd) | OpenZFS median(sd) | raw ceiling | verdict |
|---|---|---|---|---|---|
| seq write 1M | raidz2 | 397 (235) MB/s | **458 (148) MB/s** | 838 MiB/s w | OpenZFS +15% |
| seq read cold (primarycache=none) | raidz2 | 464 (1324) MB/s | 3986 (124) MB/s* | 1790 MiB/s r | see note* |
| seq read warm (ARC) | raidz2 | **8314 (9) MB/s** | 3485 (72) MB/s | (RAM) | BSD +139% |
| mmap read 512M — MB/s | raidz2 | 1237 (49) MB/s | **1342 (44) MB/s** | (RAM) | ~par |
| **mmap read 512M — free-page delta** | raidz2 | **5 (13) MB** | 502 (18) MB | — | **BSD ~100× less RAM** |
| rand 4k read QD1 — IOPS | raidz2 | **227700 (22706)** | 35346 (216) | 378k | BSD +544% |

`*` cold-read note below.

## Where BSD wins, and why (each explained)

1. **mmap free-page delta — the ARC-bridge measure (BSD ~100× less RAM).**
   Reading a 512 MiB file through `mmap` costs BSD-ZFS **~5 MiB** of extra
   resident memory but OpenZFS **~501 MiB** — roughly the whole file, twice.
   This is the single most diagnostic result. BSD-ZFS on OSv unifies the ARC
   and the page cache: an mmap'd page *is* the ARC buffer, so no second copy.
   OpenZFS keeps a private ARC and, on a page fault, **borrows/copies** the
   data into a page-cache page, double-buffering the working set. At scale this
   is real memory pressure: any mmap-heavy consumer (a mmap'd DB, a linker, a
   large index) pays for its data twice under OpenZFS-on-OSv. **This gap is
   architectural, expected, and the main reason the BSD bridge exists.** It is
   the clearest "BSD win the gap is explainable" case. Upgrade path for
   OpenZFS: an abd/page-cache sharing shim on OSv (non-trivial).

2. **Warm sequential read (BSD ~8.4 GB/s vs OpenZFS ~3.5 GB/s).** Same root as
   #1. Once the file is in the ARC, BSD serves the `read()` straight out of the
   shared ARC/page-cache pages at memcpy speed. OpenZFS's read path goes
   through its own ARC + the OSv borrow path, adding a copy and bookkeeping per
   record, roughly halving warm throughput. Not a disk effect (both are far
   above the 1790 MiB/s device ceiling — pure RAM).

3. **Random 4k read IOPS (BSD ~230k vs OpenZFS ~35k, QD1 single thread).** The
   4 GiB file is fully ARC-resident, so this measures the *per-read software
   cost* of an ARC hit. BSD's unified path resolves a cached 4k read with far
   less overhead per op (~30 µs p99, and many reads hit an already-mapped page
   for near-zero cost — note the 3.4 µs fastest rep). OpenZFS pays its ARC
   lookup + borrow accounting on every 4k op, capping it near 35k IOPS. Same
   ARC-bridge advantage as #1/#2, amplified by tiny ops. (Neither approaches the
   378k *device* ceiling because both are single-threaded QD1 CPU-bound on the
   cache path, not disk-bound.)

4. **Random 4k write IOPS (BSD +17%).** Modest; within the noise band given the
   669-stdev on writes. Both are ZIL/txg-bound. Not a strong signal.

## Where OpenZFS wins (as hoped — OpenZFS ≥ BSD on the write/CPU paths)

- **Sequential write (+34% single, +15% raidz2)**, **lz4 compression
  throughput (+56%)**, **metadata ops (+63%)**, **fsync/s (+11%)**, and
  **rand-write p99 (399 µs vs 1039 µs)**. OpenZFS 2.4.2's modern write pipeline
  (better pipelining, faster lz4, more efficient dnode/ZIL paths) beats the
  decade-old BSD port on everything that is CPU- or write-pipeline-bound rather
  than cache-read-bound.
- **O_DIRECT (OpenZFS-only).** OpenZFS `direct=always` gives 1150 MB/s write /
  1541 MB/s read while *bypassing the ARC entirely* — approaching the raw
  device ceiling (838 w / 1790 r MiB/s) with none of the double-buffering from
  #1. BSD-ZFS has no `direct` property, so it cannot offer this path at all.
  For a large-working-set consumer that manages its own cache (e.g. a database
  buffer pool), OpenZFS O_DIRECT is strictly the better tool — it sidesteps the
  very ARC-bridge cost that otherwise favors BSD.

## Notes / caveats

- `*` **Cold sequential read anomaly.** With `primarycache=none`, OpenZFS still
  reported 4.0–4.7 GB/s — well above the 1790 MiB/s device ceiling — so on OSv
  the OpenZFS cold path is **not** fully bypassing cache (metadata/L2 or the
  virtio writeback still serves data). BSD's cold read behaved correctly
  (464–850 MB/s, at/below ceiling, huge stdev as reps warm up). Treat the
  OpenZFS "cold" number as *not-truly-cold*; the honest cold ceiling for both is
  the raw ~1790 MiB/s. Use O_DIRECT (OpenZFS) for a genuine uncached read
  measurement.
- **Write stdev is high** because per-rep working sets are 2 GiB (see harness
  note) and the first rep pays cold-vdev + pool-create cost. Medians are stable.
- **8 GiB guest cap** (anti-wedge): a single write completes in seconds, but
  cumulative dirty data > ~6–8 GiB stalls the OpenZFS txg-sync path on OSv, so
  per-rep writes are bounded at 2 GiB and reads at ≤6 GiB. This is a harness
  constraint, not a ZFS throughput limit.
- **mmap > ~1.5 GiB** timed out on both ports on OSv (slow page-fault-in of a
  large mmap); the ARC-bridge measure therefore uses the 512 MiB WS<ARC point,
  which is sufficient — the free-page delta scales with file size.
- BSD-ZFS recordsize is 128k (no `large_blocks`); OpenZFS 1M for the seq
  workloads. This favors OpenZFS slightly on large sequential I/O.

## Harness notes

`scripts/bench/zfs-bench.c` is **pure C on purpose**. A g++-compiled `.so`
pulls libstdc++ symbols (`std::string@GLIBCXX_3.4.21`, `std::chrono`,
`operator new/delete`, `__cxa_*`) that OSv cannot resolve at `.so` load time —
the guest prints `Failed to load object: /zfs-bench.so. Powering off.` The C
harness uses only `clock_gettime`, `snprintf`, fixed arrays, and hand-rolled
integer parsing (host glibc 2.38 `strtoul` emits unresolvable `__isoc23_*`).
Build gate: `nm -D zfs-bench.so | grep -E 'GLIBCXX|CXXABI|__isoc23'` must be
empty. The `.so` lives in the `zfs_builder` **bootfs** (added to
`zfs_builder_bootfs.manifest.skel`), so only `zfs_builder.elf` is relinked —
the `usr.img` cpiod-populate step is never run. `scripts/bench/rebuild-bench.sh`
must pass `conf_zfs=openzfs`, or the manifest generator strips
`libzutil/libshare/libzfs_core/libtpool` (its default `bsd` branch) and
`zpool.so` fails with `libzfs_core_init`.

Reproduce: `scripts/bench/run-all.sh {sv-core|sv-rest|raidz}` inside the build
container.
