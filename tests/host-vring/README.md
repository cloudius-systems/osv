# Malformed vring regression (host)

Run on a Linux build host with G++ and Python 3:

```
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  python3 tests/host-vring/run.py
```

The runner compiles the production `drivers/virtio-vring.hh` and
`drivers/virtio-vring.cc` with their includes removed, against host stand-ins
for the device, address translation, allocation, tracing and the scheduler
(`boundary.hh`), with AddressSanitizer and UndefinedBehaviorSanitizer.  It does
not exercise real DMA, a scheduler, or concurrent consumers.  The checks stay
active with `SANITIZE=-DNDEBUG`.

Each malformed case must end in OSv's `abort()` stand-in (host exit 86) with
the expected message and with the ring's cursors, free list and descriptors
unchanged.  A normal return, a different message, an assertion failure, a
sanitizer error, a signal or a timeout fails the case.

The cases: a used ID equal to the ring size, and UINT32_MAX, followed by a
valid completion (direct and indirect descriptors); the used ID corrupted
between consume and GC (same values, direct and indirect); and a corrupted
direct NEXT link (ring size, UINT16_MAX, a self-cycle, a three-node cycle and a
full-ring cycle).  The valid controls run 70,000 completions per mode, use the
whole eight-descriptor ring (direct) or a nine-entry indirect table, and cross
both 16-bit cursor wraps.

A malformed used ID, or an invalid or cyclic direct chain, stops the guest:
there is no common queue reset that would let the driver recover the request
safely.  This guards against a buggy device or backend; it does not validate
every field a device writes (for example, an in-range but duplicate ID, or the
used index itself).
