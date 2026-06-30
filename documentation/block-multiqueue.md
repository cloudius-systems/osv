# VirtIO Block Multiqueue I/O

## Overview

The virtio-blk driver can negotiate `VIRTIO_BLK_F_MQ` with the host and
operate multiple virtqueues, one selected per submitting CPU.  This lets
several vCPUs submit block I/O in parallel against independent rings
instead of serialising on a single queue lock.

The implementation lives entirely in `drivers/virtio-blk.cc` and
`drivers/virtio-blk.hh`; there is no separate generic block-multiqueue
framework.  Each queue is an ordinary virtio virtqueue guarded by its own
mutex.

## Queue assignment

A request is steered to a queue by the id of the CPU that submits it:

```c
int qid = sched::cpu::current()->id % _num_queues;
```

So a given CPU consistently uses the same ring, which keeps the request's
data structures warm in that CPU's cache and avoids cross-CPU contention
on the common path.

## Feature negotiation and initialization

When the host offers `VIRTIO_BLK_F_MQ`, the driver reads `num_queues`
from the device config and `probe_virt_queues()` sets up that many
virtqueues.  The probed count is authoritative: a per-queue lock vector
(`_queue_locks`) is sized to match, and a single completion thread services
all of them.

```c
probe_virt_queues();
_num_queues = virtio_driver::_num_queues;
...
_queue_locks = std::vector<mutex>(_num_queues);
```

## Submission path

`make_request()` selects the queue by CPU id and holds only that queue's
lock while building and kicking the descriptor chain:

```c
int qid = sched::cpu::current()->id % _num_queues;
WITH_LOCK(_queue_locks[qid]) {
    auto* queue = get_virt_queue(qid);
    // build descriptor, add to ring, kick
}
```

Two CPUs mapped to different queues never block each other here; two CPUs
mapped to the same queue (more vCPUs than queues) share that queue's lock.

## Completion path

The current model uses a single device interrupt.  One completion thread
sleeps until any queue's used ring has pending completions; when woken it
drains every queue, taking each queue's lock so it cannot race with that
queue's owner:

```c
for (int q = 0; q < _num_queues; q++) {
    WITH_LOCK(_queue_locks[q]) {
        drain_queue(get_virt_queue(q));
        get_virt_queue(q)->wakeup_waiter();
    }
}
```

The wake predicate checks every queue's used ring and re-arms interrupts on
all queues when none have work, so a completion landing on any queue wakes
the single thread.

### Known limitation

Because only one MSI/legacy interrupt vector is wired, completions for all
queues are processed from the CPU that takes the interrupt (typically
CPU 0), which then reaches across queues on the owners' behalf.  A future
change can register a per-queue interrupt (as the NVMe driver does via
`driver::register_io_interrupt()`), so each queue's completions land on
its owning CPU and the cross-queue drain loop can be dropped.  The
submission path already scales; only completion handling is centralised.

## Configuration

Enable multiqueue in QEMU by giving the device more than one queue and the
guest more than one vCPU:

```bash
qemu-system-x86_64 \
  -device virtio-blk-pci,drive=drive0,num-queues=4 \
  -drive file=disk.img,if=none,id=drive0 \
  -smp 4 \
  ...
```

`scripts/run.py` exposes this through the `--virtio-blk-queues` option so
test images can request multiple queues without hand-editing the QEMU
command line.  With one vCPU or one queue the driver behaves exactly as the
original single-queue path.

## Testing

Boot any image on a multi-vCPU guest with `num-queues > 1` (see the QEMU
invocation above) and drive concurrent I/O from multiple threads; each
submitting CPU exercises an independent ring.  The driver falls back to the
single-queue path automatically when the host offers one queue or the guest
has one vCPU, so existing single-queue tests continue to pass unchanged.

## References

- [VirtIO Block device specification](https://docs.oasis-open.org/virtio/virtio/v1.1/cs01/virtio-v1.1-cs01.html#x1-2430006)
