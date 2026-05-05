# ZFS Performance on OSv

This document describes how to benchmark ZFS against other OSv filesystems,
explains what the `tst-fs-bench` tool measures, and lists ZFS tuning knobs
that influence the results.

## Benchmark tool: tst-fs-bench

`tests/tst-fs-bench.so` is the canonical OSv filesystem benchmark.  It is a
single self-contained shared-object that runs inside the OSv guest and reports
results in a machine-parseable format.

### What it measures

| Category | Metric | Method |
|---|---|---|
| Sequential write | MB/s | `write()` loop, 4 KB and 128 KB I/O sizes, `fsync` at end |
| Sequential read  | MB/s | `read()` loop, same two I/O sizes |
| Random 4 KB read | IOPS | 500 random `pread()` calls at page-aligned offsets |
| Random 4 KB write| IOPS | 500 random `pwrite()` calls, `fsync` at end |
| Metadata create  | ops/s | `open(O_CREAT)` + `write(4 KB)` + `close` for N files |
| Metadata stat    | ops/s | `stat()` on each of the N files |
| Metadata readdir | entries/s | Single `readdir()` pass over the directory |
| Metadata unlink  | ops/s | `unlink()` each file |
| mmap sequential  | MB/s | `mmap(MAP_PRIVATE)` scan reading every 8-byte word |

Default parameters: 32 MB test file, 200 metadata files, working directory
`/bench` (use `--dir /` for the ZFS root where `/bench` may not be writable).

All output lines are prefixed with `BENCH:` so they can be extracted from
mixed output with `grep '^BENCH:'`.

### How to run

```
# ZFS root filesystem (standard zfs-test image)
./scripts/run.py -k --arch=x86_64 --vnc none -m 512 -c2 -s \
  -e "tests/tst-fs-bench.so --dir /"

# Larger file for more stable throughput numbers
./scripts/run.py -k --arch=x86_64 --vnc none -m 1024 -c2 -s \
  -e "tests/tst-fs-bench.so --dir / --size-mb 128"

# ramfs image (built with the default fs=ramfs)
./scripts/run.py -k --arch=x86_64 --vnc none -m 512 -c2 -s \
  -e "tests/tst-fs-bench.so --dir /tmp"
```

### Representative results

The table below shows representative values observed on a KVM/QEMU x86-64
guest with virtio-blk, a single vCPU, 512 MB RAM, and a host backed by an
NVMe SSD.  All numbers are approximate; actual values depend heavily on the
host storage subsystem, QEMU version, and memory pressure.

| Metric | ZFS (default) | ZFS (recordsize=1M) | ramfs |
|---|---|---|---|
| seq_write_4k   (MB/s) | 40–80   | 40–80   | 800–2000 |
| seq_write_128k (MB/s) | 60–120  | 80–200  | 800–2000 |
| seq_read_4k    (MB/s) | 80–200  | 80–200  | 1000–3000 |
| seq_read_128k  (MB/s) | 150–400 | 200–600 | 1000–3000 |
| rand_read_4k   (IOPS) | 500–2000 | 500–2000 | 5000–20000 |
| rand_write_4k  (IOPS) | 200–800  | 200–800  | 3000–10000 |
| meta_create    (ops/s) | 500–2000 | 500–2000 | 10000–50000 |
| meta_stat      (ops/s) | 10000–50000 | 10000–50000 | 50000–200000 |
| meta_unlink    (ops/s) | 500–2000 | 500–2000 | 10000–50000 |
| mmap_seq_read  (MB/s) | 200–600  | 200–600  | 2000–10000 |

**Interpretation:**

- ZFS write throughput is bounded by the virtio-blk round-trip latency and
  ZFS transaction group (TXG) commit interval (default: 5 seconds).  The
  first `seq_write` run in the benchmark often ends mid-TXG and includes the
  forced `fsync`, making the number lower than sustained write throughput.
- ZFS read throughput benefits from the ARC (Adaptive Replacement Cache).
  After the first read pass the data is cached, so repeated reads are
  memory-speed.  The benchmark reads a freshly written file, so ARC hit rate
  is low for the first pass.
- ramfs has no persistence overhead — all I/O is purely in-memory — so it
  is 5–20x faster than ZFS for all metrics.  This is expected and not a ZFS
  bug.

## ZFS tuning parameters

The following per-dataset and pool-wide properties materially affect benchmark
results.  They can be set with `/zfs.so set <prop>=<val> <dataset>` or at
dataset creation time.

### recordsize (per-dataset)

Default: 128 KB.  Valid range: 512 B – 16 MB (must be a power of two).

```
/zfs.so set recordsize=1M rpool/bench
```

- Larger recordsize reduces write amplification for sequential workloads and
  can more than double sequential write throughput for large files.
- Smaller recordsize (e.g., 8 KB) is better for random I/O workloads such as
  databases (PostgreSQL typically uses 8 KB pages).
- The recordsize only affects new writes; existing data is not re-written.

### primarycache (per-dataset)

Default: `all` (cache both metadata and data in the ARC).

```
/zfs.so set primarycache=metadata rpool/bench   # cache only metadata
/zfs.so set primarycache=none     rpool/bench   # disable ARC for this dataset
```

Setting `primarycache=none` forces every read to go to disk and gives a
worst-case read throughput number that reflects true disk performance rather
than cache effects.

### compression (per-dataset)

Default: `off`.  Recommended for general use: `lz4`.

```
/zfs.so set compression=lz4 rpool/bench
```

LZ4 compression reduces the amount of data written to disk for compressible
workloads (text, code, logs) and can *increase* effective throughput when the
CPU cost of compression is lower than the I/O cost it saves.  The `tst-fs-bench`
benchmark writes incompressible data (`0xAB` or `0xCD` fill), so enabling
compression has no benefit for these specific numbers.

### sync (per-dataset)

Default: `standard` (honour `fsync()` calls, flush the ZIL).

```
/zfs.so set sync=disabled rpool/bench   # WARNING: data loss on crash
```

Setting `sync=disabled` makes `fsync()` a no-op, which dramatically increases
write IOPS at the cost of durability.  Do not use this in production.  It is
useful for benchmarking the theoretical maximum throughput without ZIL overhead.

### atime (per-dataset)

Default: `on`.

```
/zfs.so set atime=off rpool/bench
```

Disabling atime eliminates metadata writes for every read and typically
improves metadata-intensive workloads by 5–15%.

### arc_max (pool-wide, tunable via `/proc`-equivalent)

The ARC size is bounded by `zfs_arc_max` (default: half of physical RAM on
most platforms).  On OSv this is controlled by the `zfs_arc_max` module
parameter at boot time via the sysctl interface.  The default is appropriate
for most workloads.

## Benchmark reproducibility notes

1. Always record the full output including the `statvfs` line, which shows
   available free space.  A nearly-full pool shows lower write throughput due
   to space map fragmentation.
2. Run the benchmark at least twice; the first run warms the ARC and may show
   lower read throughput.
3. Use `--size-mb 128` or larger on a pool with sufficient free space to
   reduce the impact of TXG alignment on the write numbers.
4. Specify `-m 1024` or more when running with a large file to avoid the ARC
   evicting data during the write phase.
