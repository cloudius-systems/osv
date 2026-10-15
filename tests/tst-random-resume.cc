/*
 * Copyright (C) 2026 Greg Burd
 * BSD license; see LICENSE in the top-level directory.
 */

// Opt-in, destructive test: fresh single-CPU guest, no hardware entropy source.
// Never include in the default suite: the harvester remains disabled on exit.
// Test the real adaptor gate, not VFS (which currently drops O_NONBLOCK).
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <sys/types.h>
#include <osv/sched.hh>
#include <sys/param.h>
#include <sys/mutex.h>
#include <dev/random/randomdev.h>
#include <dev/random/randomdev_soft.h>
#include <dev/random/random_adaptors.h>
#include <dev/random/live_entropy_sources.h>
#include <drivers/random.hh>

int main()
{
    assert(sched::cpus.size() == 1);
    assert(live_entropy_sources_empty());
    // Stop credited interrupt events through the actual registration API.
    // Single CPU prevents a concurrent producer retaining the old callback.
    randomdev_deinit_harvester();
    random_adaptor->reseed(); // drain queued interrupt events
    mtx_lock(&random_reseed_mtx);
    random_adaptor->seeded = 1;
    mtx_unlock(&random_reseed_mtx);
    random_adaptor->reseed(); // clear any remaining pool credits
    mtx_lock(&random_reseed_mtx);
    random_adaptor->seeded = 0;
    mtx_unlock(&random_reseed_mtx);

    assert(random_adaptor->block(O_NONBLOCK) == EWOULDBLOCK);
    puts("CONTROL: no live source, interrupt harvest disabled, gate unseeded");
    randomdev::reseed_on_resume();
    assert(random_adaptor->block(O_NONBLOCK) == EWOULDBLOCK);
    puts("PASS: actual zero-credit resume hook preserves unseeded adaptor gate");
}
