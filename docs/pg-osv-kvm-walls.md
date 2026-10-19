OSv + stock PostgreSQL on KVM: the three walls to a real workload, resolved
===========================================================================

This documents how a stock musl PostgreSQL 18 image, built from the committed
integ/pg-fork-zfs tip (CONF_fork=1, fs=zfs, conf_zfs=openzfs), serves a real
pgbench workload on OSv under KVM -- with NO uncommitted deltas and NO
PostgreSQL configuration workaround required.

Serving config (nothing special, PostgreSQL defaults):
  - shared_memory_type = mmap        (the PostgreSQL default -- NOT sysv)
  - dynamic_shared_memory_type = posix
  - listen_addresses = '*', unix_socket_directories = '' (OSv has no AF_UNIX)
  - boot with --env=LC_ALL=C (musl has no C.UTF-8 locale data), --extra-zfs-pools

Wall 1 -- anonymous MAP_SHARED populate spin loop (FIXED, kernel)
-----------------------------------------------------------------
Symptom: with the default shared_memory_type=mmap, the postmaster hangs at
IpcMemoryCreate -> mmap(MAP_SHARED|MAP_ANONYMOUS) (sysv_shmem.c) at 99.9% CPU
before "ready to accept connections". Reproduced across every shared_buffers
size and smp count; sysv shm avoided it, which mislabelled it a config choice.

Root cause: OSv backs an anonymous MAP_SHARED region under CONF_fork with the
fork-coherent shared_anon_page_provider, whose page registry is keyed on the 4K
page VA, so it must fill 4K pages one at a time and returns false from its
level-1 (2M) map() to force that. But populate::page() ignored that false: it
always returned true, telling the page-table walker the 2M range was handled.
The walker then neither descended to level 0 nor left an intermediate table, so
the 2M PTE stayed empty. The faulting instruction retried on the same address
forever -- the spin loop.

Fix (committed): when a provider's map() returns false at a large-capable
level, populate::page() returns false too, so the walker allocates the level-1
intermediate table and descends to the 4K map(). Gated under CONF_fork; the
non-fork build is behaviorally unchanged. See core/mmu.cc populate::page().

Wall 2 -- catalog reads return zero pages across forked backends (already FIXED)
--------------------------------------------------------------------------------
Symptom (with the sysv workaround): "select 1" works, but any catalog query in
a forked backend fails -- FATAL role "postgres" does not exist / index contains
unexpected zero page. The forked backend read a shared_buffers catalog page as
zeros because the shared segment was not fork-coherent.

Resolution: this is exactly what the committed shared_anon_page_provider
(the CONF_fork anonymous-MAP_SHARED coherence path in core/mmu.cc) solves: every
address space -- postmaster and all fork children -- resolves a given shared
page VA to the same physical frame via a process-global VA-keyed registry.

The crux was the W1<->W2 interaction: the sysv workaround routes PostgreSQL's
shared memory through SysV shm, a DIFFERENT OSv path that the coherence provider
does NOT cover -- so sysv dodged W1 but re-opened W2. Fixing W1 lets PostgreSQL
use its DEFAULT mmap shared memory, which flows through the coherent provider,
so a forked backend reads the same catalog pages the postmaster filled. No sysv,
no new W2 code -- W1's fix routes the workload back onto the already-coherent
path.

Wall 3 -- kernel MM fault in page_range_allocator::remove (downstream of W1/W2)
-------------------------------------------------------------------------------
Symptom (previously): under load, an OSv page_fault in
memory::page_range_allocator::remove <- page_pool::l2::free_batch <- l2::unfill
<- l2::fill_thread ("Aborted").

Resolution: this did not reproduce once W1 was fixed. It was a downstream effect
of the W1 mmap-populate spin: the endless 2M fault loop churned the physical
page pools pathologically (repeated large-page alloc attempts + teardown under a
pinned-CPU spin), destabilising the L2 pool fill/unfill path. With the populate
loop gone, the allocator is exercised normally and the fault does not occur.
Confirmed gone under pgbench -i -s50 + pgbench -c8 -T30 (0 failed) and boot 5/5.

Validation (from the committed tip + the W1 fix, default mmap shared memory)
----------------------------------------------------------------------------
  psql catalog queries (each a forked backend):
    select current_database()          -> postgres
    select current_user                -> postgres        (pg_authid, no zero page)
    select count(*) from pg_class       -> 415             (was: unexpected zero page)
    select rolname from pg_roles        -> 17 roles incl. postgres
    information_schema.tables count      -> 213
    CREATE TABLE t + INSERT 1000 + SELECT count(*) -> 1000
  pgbench -i -s50   -> 5,000,000 tuples loaded, vacuum + PKs, rc=0
  pgbench -c8 -T30  -> 37,281 transactions, 0 failed (0.000%), tps ~1252
  boot 5/5          -> "database system is ready to accept connections", ~500 ms each
  no page_range_allocator / vm_fault / zero-page across the run

Reproducing the serving image (from committed code, no uncommitted deltas)
--------------------------------------------------------------------------
1. Build integ/pg-fork-zfs tip (this file's commit or later); submodules
   musl_1.2.1 / musl_0.9.12 / kbuild / acpica / open_zfs/openzfs. Recreate the
   apps pg18-fork module (base apps = cloudius master).
2. Build PostgreSQL 18 as a musl PIE (CC=musl-gcc, -fPIC -DWAIT_USE_SELF_PIPE,
   LDFLAGS_EX=-pie) with the two OSv neuters (check_root, checkDataDir), install
   to /b/.local/pg18/install.
3. Build cpiod.so, then image=zfs-tools,pg18-fork fs=zfs conf_zfs=openzfs
   conf_fork=1. Verify the manifest has bin/postgres + /tools/cpiod.so +
   plpgsql.so before boot.
4. host-initdb a cluster (LOCALE_PROVIDER builtin, BUILTIN_LOCALE C, -E UTF8),
   leaving shared_memory_type at its default (mmap). Add HBA trust for the guest
   NAT subnet. Seed a ZFS pool on a second virtio-blk disk (zpool create pgdata;
   mountpoint=/data; cpiod stream the cluster).
5. Boot: postgres -D <cluster>, --extra-zfs-pools, --env=LC_ALL=C. PostgreSQL
   reaches ready, forks backends, and answers real catalog queries + pgbench.

Config-only setting a user must set: NONE for the walls. shared_memory_type is
left at the PostgreSQL default (mmap). The only non-default choices are the
musl-locale accommodation (builtin C locale, --env=LC_ALL=C) and TCP-only
(unix_socket_directories='') because OSv has no AF_UNIX -- both are OSv-platform
facts, not workarounds for the three walls.
