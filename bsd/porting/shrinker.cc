/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mempool.hh>
#include <osv/debug.hh>
#include <sys/eventhandler.h>

struct eventhandler_entry_generic {
    struct eventhandler_entry ee;
    size_t   (* func)(void *arg);
};

class bsd_shrinker : public memory::shrinker {
public:
    explicit bsd_shrinker(struct eventhandler_entry_generic *ee);
    size_t request_memory(size_t s);
    virtual size_t release_memory(size_t s) { return 0; }
private:
    struct eventhandler_entry_generic *_ee;
};

bsd_shrinker::bsd_shrinker(struct eventhandler_entry_generic *ee)
    : shrinker("BSD"), _ee(ee)
{
}

size_t bsd_shrinker::request_memory(size_t s)
{
    // Return the amount of released memory.
    return _ee->func(_ee->ee.ee_arg);
}

void bsd_shrinker_init(void)
{
    struct eventhandler_list *list = eventhandler_find_list("vm_lowmem");
    struct eventhandler_entry *ep;

    debug("BSD shrinker: event handler list found: %p\n", list);

    TAILQ_FOREACH(ep, &list->el_entries, ee_link) {
        debug("\tBSD shrinker found: %p\n",
                ((struct eventhandler_entry_generic *)ep)->func);

        new bsd_shrinker((struct eventhandler_entry_generic *)ep);
    }
    EHL_UNLOCK(list);

    debug("BSD shrinker: unlocked, running\n");
}
