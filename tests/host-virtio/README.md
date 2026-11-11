# Virtio block full-ring regression

Copyright (C) 2026 Greg Burd. BSD license; see the top-level LICENSE.

Run on Linux with Python 3 and g++:

```
python3 tests/host-virtio/run.py
SANITIZE='-fsanitize=address,undefined -fno-omit-frame-pointer' python3 tests/host-virtio/run.py
SANITIZE='-fsanitize=thread -fno-omit-frame-pointer' python3 tests/host-virtio/run.py
```

The runner compiles the current production vring header and implementation,
plus the actual block make_request, drain_queue, any_queue_not_empty and
req_done methods. Kernel scheduling, physical mapping, interrupt wake handles,
tracepoints and bio completion are host boundaries; no ring algorithm is
reimplemented. A two-descriptor direct ring fits one FLUSH. A second producer
must enter add_buf_wait while holding its production submission mutex before
the backend publishes the first completion. Both submissions must complete
and every descriptor must be reclaimed. Reintroducing the consumer mutex
causes the five-second alarm to fire.

The scheduler shim polls instead of sleeping: this checks the lock dependency
and shared-memory publication, not OSv wakeup delivery. TSan cannot model the
device-facing atomic fences. Kernel enabled builds and real guest full-ring,
interrupt/rearm, multi-CPU and indirect-descriptor stress remain separate
validation requirements. This is not kernel/runtime validation or a performance
test. Test extraction intentionally fails loudly if production method names
or structure cease matching; production objects must also be compiled.
