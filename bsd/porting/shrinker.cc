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
private:
    struct eventhandler_entry_generic *_ee;
};

class arc_shrinker : public memory::shrinker {
public:
    explicit arc_shrinker();
    size_t request_memory(size_t s);
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

arc_shrinker::arc_shrinker()
    : shrinker("ARC")
{
}

extern "C" size_t arc_lowmem(void *arg, int howto);
size_t arc_shrinker::request_memory(size_t s)
{
    return arc_lowmem(nullptr, 0);
}

void bsd_shrinker_init(void)
{
    struct eventhandler_list *list = eventhandler_find_list("vm_lowmem");
    struct eventhandler_entry *ep;

    debug("BSD shrinker: event handler list found: %p\n", list);

    TAILQ_FOREACH(ep, &list->el_entries, ee_link) {
        debug("\tBSD shrinker found: %p\n",
                ((struct eventhandler_entry_generic *)ep)->func);

        auto *_ee = (struct eventhandler_entry_generic *)ep;

        if ((void *)_ee->func == (void *)arc_lowmem) {
            new arc_shrinker();
        } else {
            new bsd_shrinker(_ee);
        }
    }
    EHL_UNLOCK(list);

    debug("BSD shrinker: unlocked, running\n");
}
