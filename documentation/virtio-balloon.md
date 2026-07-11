# virtio-balloon

OSv implements the virtio memory balloon (`virtio-balloon`, virtio device id 5),
which lets the host reclaim guest memory on demand and give it back later. This
is useful for memory overcommit and density: the host can shrink an idle guest
and grow it again under load, without stopping it.

## How it works

The driver has two virtqueues, inflate (0) and deflate (1), and reads the target
balloon size from the device config (`num_pages`). A worker thread polls the
target and moves the balloon toward it:

- **Inflate** (host raised `num_pages`): the driver allocates guest pages with
  `memory::alloc_page()`, hands their PFNs to the host on the inflate queue, and
  keeps them out of OSv's free pool (the memory is now the host's).
- **Deflate** (host lowered `num_pages`): the driver returns ballooned pages'
  PFNs on the deflate queue and frees them back to OSv with
  `memory::free_page()`.

After each adjustment the driver writes the current balloon size back to the
config `actual` field so the host can track it.

The worker polls on a short interval rather than waking on a config-change
interrupt, because OSv's virtio core does not currently wire the MSI-X
config-change vector. Ballooning is not latency-critical, so a sub-second
reaction is fine.

## Trying it

Boot OSv with a balloon device and a QMP socket:

```
qemu-system-x86_64 ... \
  -device virtio-balloon-pci,id=balloon0 \
  -qmp tcp:127.0.0.1:4444,server,nowait
```

Run `tests/misc-balloon.so`, which prints free memory once a second. From the
host, shrink the guest (inflate the balloon) and then grow it back (deflate):

```
{"execute":"qmp_capabilities"}
{"execute":"balloon","arguments":{"value":536870912}}   # inflate: give up memory
{"execute":"balloon","arguments":{"value":1073741824}}  # deflate: take it back
{"execute":"query-balloon"}
```

Free memory drops after the inflate and recovers after the deflate.
