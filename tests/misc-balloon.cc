/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Manual helper for exercising the virtio-balloon driver.  The balloon target
// is controlled by the host, so this cannot be a self-checking automated test;
// instead it prints free memory once a second while a host-side harness drives
// the balloon over QMP.  Boot it with a balloon device, e.g.:
//
//   qemu ... -device virtio-balloon-pci,id=balloon0 \
//            -qmp tcp:127.0.0.1:4444,server,nowait
//
// then from the host:  {"execute":"balloon","arguments":{"value":<bytes>}}
//
// Free memory should drop when the host shrinks the guest (inflate) and recover
// when it grows it back (deflate).

#include <osv/mempool.hh>

#include <unistd.h>
#include <cstdio>

int main()
{
    for (int i = 0; i < 30; i++) {
        printf("balloon: t=%d free=%lu bytes\n", i, memory::stats::free());
        fflush(stdout);
        sleep(1);
    }
    return 0;
}
