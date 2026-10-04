<!-- Copyright (C) 2026 Greg Burd. -->
# Crucible admission and failure policy

This client speaks unencrypted Crucible V13. Configure three **numeric IPv4**
addresses (`ip:port`); hostname resolution is not supported because synchronous
DNS cannot be cancelled safely. Admission has one total five-second steady-clock
deadline across all three peers: connect and every handshake send/receive share
it, without resetting on a new peer, stage, retry or partial progress.
After admission, a send or partial incoming frame has its own five-second total
deadline; it does not inherit the expired admission deadline. Slow
healthy servers can therefore be quarantined; this is a safety-first client,
not the availability policy of the Rust upstairs.

Every volume requires an explicit operator-owned nonzero uint64 generation:

    --crucible=127.0.0.1:8810,127.0.0.1:8820,127.0.0.1:8830 \
    --crucible-uuid=<uuid> --crucible-generation=2

Indexed volumes use `--crucible0-generation=2` through
`--crucible7-generation=...`, alongside their indexed targets and UUID options.
No generation is inferred or incremented automatically. The C++
`crucible_init(..., device_index, generation)` and `UpsairsClient` APIs accept the
same explicit authority; omitted/zero generation refuses admission.

**The operator must lease the generation externally and fence previous writers.**
A larger integer is not proof that another upstairs cannot write. Use a value
strictly above every persisted extent generation after externally establishing
exclusive ownership. A clean generation-1 session normally needs a higher leased
generation on reopening. These options do not supply a lease service or automatic
split-brain protection.

Admission requires all three replicas, matching geometry, identical clean extent
metadata, and a sufficient supplied generation. Dirty or divergent regions require
external reconciliation; even three matching dirty regions are refused. In-session
failure permanently quarantines that replica. Two surviving admitted peers may
continue; no automatic reconnect, live repair, or degraded initial admission exists.
A failed/ambiguous operation fences the entire client session before another job
can be admitted. Reusing that client object is forbidden.

Jobs are serialized, with a transitive dependency on the preceding job including
reads and flushes. Public I/O is limited to 1 MiB, matching the block device split.
Each sender retains at most 32 queued frames / 4 MiB plus one active frame; exceeding
this cap quarantines the lagging peer. Snapshots require all three acknowledgments.
Destruction requires external callers to be quiesced; disconnect cancels and joins
owned workers and excludes admission. No detached recovery threads exist.

Protocol fixtures and host pthread shims are not proof of OSv guest lifetime,
server disk durability, crash consistency, or external fencing.
