# Malformed vring regression (host)

Run on a Linux build host with G++ and Python 3:

```
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/host-vring/run.py
```

The runner compiles the **complete production vring header and implementation**
with only includes removed. No ring algorithm is copied or replaced. Device,
physical-address translation, allocation, trace and scheduler boundaries are
host substitutes; no real DMA, scheduler, concurrent consumer, or guest shutdown
is tested. The only successful negative outcome is the noreturn OSv fatal
boundary (host exit 86), after matching the exact formatted reason and checking
no cursor/availability/free publication or descriptor modification. Ordinary
return, unrelated fatal reason, assertion failure, sanitizer error, signal and
timeout fail.
Checks remain active with `SANITIZE=-DNDEBUG`.

Cases cover device-owned used IDs equal to ring size and UINT32_MAX followed by
a valid completion (direct/indirect), corruption of the used ID between consume
and GC (size and UINT32_MAX, direct/indirect), guest-owned direct NEXT corruption
(size and UINT16_MAX, self-cycle, three-node and full-ring cycles), and 70,000
valid completions per mode. Direct
controls consume the entire eight-descriptor ring; indirect controls force a
nine-SG table, verify its actual allocation/free and reclaim all descriptors.
The controls cross both 16-bit consumer/GC cursor wraps. An empty queue returns
null without writing length. No property-testing dependency is needed for this
small deterministic fault matrix.

## Contract and limits

Malformed used IDs and invalid/cyclic **direct GC chains** terminate the guest
through OSv's existing noreturn `abort(fmt, ...)`. They are not skipped and do
not fabricate a completion or reclaim a chain of unknown ownership. There is
no proven common reset/cancel/join API for all queue users; writing device status
zero is not sufficient recovery. The guest therefore remains vulnerable to
**denial of service** by a broken/malicious backend.

A device owns used IDs; direct descriptor links are guest-owned. Corrupt NEXT
requires a separate internal corruption or DMA/backend violation, not merely
an invalid used ID. This patch is not malicious-device containment: the
hypervisor/DMA remains trusted. In-range duplicate/unsubmitted IDs, queue-size
validation, arbitrary used-index progress, payload lengths, indirect table
addresses/flags, free-list corruption in submission, and concurrent backend
rewrites are not validated here. In particular GC frees a valid indirect table
without walking its links; it cannot validate arbitrary corrupted pointers.
No fix or approval of those separate fields/ownership/lifecycle issues follows.
