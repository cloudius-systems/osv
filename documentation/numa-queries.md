# NUMA query compatibility subset

Topology discovery and these queries do not activate NUMA allocation or scheduling.
`getcpu` returns one snapshot's scheduler CPU ID and compact node ID; unknown CPU
membership retains node0 compatibility fallback, not proven affinity.
`numa::node_of_phys` returns a firmware physical-range owner or -1, never a
virtual-address translation or guessed node0.

`get_mempolicy` supports:

- flags0: MPOL_DEFAULT and an empty mask. This means no nondefault policy is
  installed, **not** Linux's local-allocation placement behavior.
- MPOL_F_MEMS_ALLOWED alone: all firmware-reported memory-bearing dense nodes,
  including CPU-less nodes, excluding CPU-only nodes. With unavailable topology
  the synthetic flat node0 is returned. Available topology with no memory ranges
  returns EOPNOTSUPP: unknown memory is not an empty allocation-permission set.
  OSv has no cpusets; this is a compatibility topology approximation, **not** a
  Linux cpuset permission constraint or certified inventory of allocatable RAM.
  Partial firmware descriptions do not prove other memory cannot be allocated,
  and the allocator does not enforce this mask. `addr` is ignored in this mode,
  as in native Linux; optional mode output is initialized to0.
- MPOL_F_NODE without ADDR: EINVAL under DEFAULT. Linux uses this to query the
  next interleave allocation, not the current CPU node.
- ADDR, with or without NODE: EOPNOTSUPP. No address is dereferenced, translated,
  faulted in or reported as successfully placed, including NULL and invalid
  addresses. This is deliberately not Linux's mapped-address/EFAULT behavior.
- Unknown bits or MEMS_ALLOWED combined with NODE/ADDR: EINVAL. Nonnull `addr`
  without ADDR (except MEMS_ALLOWED) is EINVAL.

`set_mempolicy` accepts only unmodified MPOL_DEFAULT and an effective empty mask.
This resets no placement state because no nondefault policy can be installed.
Modes PREFERRED, BIND, INTERLEAVE, LOCAL, PREFERRED_MANY, WEIGHTED_INTERLEAVE and
recognized STATIC_NODES/RELATIVE_NODES modifiers return EOPNOTSUPP, even when
empty. Unknown mode/bits, STATIC+RELATIVE together, and NUMA_BALANCING except
with BIND/PREFERRED_MANY return EINVAL. Recognized unsupported requests fail
before reading the mask; their Linux placement validation is not emulated.
No successful no-op BIND or preferred placement is advertised.

## Mask count convention and deliberate native divergence (64-bit only)

The native Linux implementation starts with `maxnode - 1` input bits and
`ALIGN(maxnode - 1, 64) / 8` output bytes. libnuma passes `mask.size + 1`.
This differs at boundary values from the manual's literal maximum-ID-plus-one
and word-rounding prose. We follow that count convention and these observed
native syscall boundaries, **not every native input-padding validation rule**:

| Nonnull mask / maxnode | get | set DEFAULT |
| --- | --- | --- |
| 0 | EINVAL | EINVAL |
| 1 | writes no mask bytes if topology count permits | ignores mask |
| 65 | writes8 bytes | examines64 bits |
| 66 | writes16 bytes | examines65 bits; padding ignored |

Get requires maxnode >= discovered dense node count before flag evaluation,
including CPU-only domains. Get initializes every returned word and clears
excess words. Get need not truncate the last word to maxnode-1 bits, matching
native Linux output copying. OSv DEFAULT reset uniformly masks off padding in
the last effective word. **This deliberately diverges from Linux above its
build-time MAX_NUMNODES bitmap capacity.** Linux's high-word validation can
reject set bits outside the effective count. MAX_NUMNODES is neither the current
node count nor OSv's discovery budget256; OSv does not import a1024-node ABI cap
from the test host. The following real Debian6.12.111 x64 results used
CONFIG_NODES_SHIFT10 (MAX_NUMNODES1024), one active node and Seccomp0:

| maxnode | word16 bit | Linux DEFAULT | OSv DEFAULT |
| --- | --- | --- | --- |
| 1025 | 0,1,63 | success (all beyond extent) | success |
| 1026 | 0 | EINVAL (logical bit) | EINVAL |
| 1026 | 1,63 | EINVAL (high-word padding) | success (padding ignored) |
| 1089,1090 | 0,1,63 | EINVAL (logical bits) | EINVAL |

All four empty-mask controls succeeded; input buffers and canaries were
unchanged. This table characterizes that kernel configuration, not all Linux
capacities. OSv's intentional rule remains uniformly trim to maxnode-1 bits
within the finite bound; no placement capability is implied by empty reset.

NULL masks ignore maxnode. For nonnull masks maxnode above32769 is EINVAL,
checked before arithmetic: at most4096 bytes of bitmap storage, without copying
Linux's unsigned overflow quirks. Invalid size/flag/address requests do not write
outputs in this subset. No mode is persisted after rejected setters.

Optional NULL outputs are valid. Valid unaligned buffers are copied with memcpy.
Like surrounding OSv shared-address-space syscalls (BSD copyin/copyout also use
plain memcpy), this does **not** guarantee EFAULT for arbitrary inaccessible
mask/mode buffers, or isolation from hostile pointers. No user-VA page-table
walk is added. Such fault-safe copying needs a separate cross-architecture fix.

## Evidence

- Linux v6.12 `mm/mempolicy.c`, commit
  `adc218676eef25575469234709c2d87185ca223a`: kernel_get_mempolicy,
  do_get_mempolicy, copy_nodes_to_user, get_nodes/get_bitmap, sanitize_mpol_flags.
- numactl v2.0.19 `libnuma.c`, commit
  `3bc85e37d5a30da6790cb7e8bb488bb8f679170f`: setpol/getpol,
  set_nodemask_size and numa_get_mems_allowed use size+1.
- man-pages5.13 get_mempolicy.2/set_mempolicy.2, commit
  `091fbf1fef4808f0ccfe0ff8c333aedf833b8782`: policy/flag/error descriptions;
  maxnode prose discrepancy noted above, not silently implemented instead.
- Debian native6.12.111 x64 (one NUMA node, Seccomp0) syscall canary probe
  confirmed get0/1/65/66 and DEFAULT nonempty-mask reset behavior as above.
  This is not evidence for other Linux versions or compat32.
- Independent high-word native probe source SHA256
  `8027e62f82a17e93f0d0abe26c38102ed5fefbd9424c21c18aadadfe118d0647`,
  receipt SHA256 `af889712860d3078bf33881d6c5f43da4ebb3284e8eae0529bb301db00320650`.
  `tests/tst-numa-queries.py --native-boundary-log <native-boundary.log>`
  compares all16 production results with that pinned actual receipt, requiring
  the two documented divergences rather than incorrectly requiring equality.

`python3 tests/tst-numa-queries.py` runs production syscall bodies and physical
lookup with minimal host fixtures under ASan/UBSan. `tst-numa-mempolicy.so`
exercises the compiled OSv syscall dispatcher; `tst-numa-parser.py` separately
validates discovery. No test establishes physical allocation locality.
