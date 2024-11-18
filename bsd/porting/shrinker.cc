/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mempool.hh>
#include <osv/debug.hh>
#include <osv/export.h>
#include <sys/eventhandler.h>
#include <algorithm>

struct eventhandler_entry_generic {
    struct eventhandler_entry ee;
    size_t   (* func)(void *arg);
};

class bsd_shrinker : public memory::shrinker {
public:
    explicit bsd_shrinker(struct eventhandler_entry_generic *ee);
    size_t request_memory(size_t s, bool hard);
private:
    struct eventhandler_entry_generic *_ee;
};

class arc_shrinker : public memory::shrinker {
public:
    explicit arc_shrinker();
    size_t request_memory(size_t s, bool hard);
};

bsd_shrinker::bsd_shrinker(struct eventhandler_entry_generic *ee)
    : shrinker("BSD"), _ee(ee)
{
}

size_t bsd_shrinker::request_memory(size_t s, bool hard)
{
    // Return the amount of released memory.
    return _ee->func(_ee->ee.ee_arg);
}

arc_shrinker::arc_shrinker()
    : shrinker("ARC")
{
}

//These two function pointers will be set dynamically in INIT function of
//libsolaris.so by calling register_shrinker_funs() below. The arc_lowmem()
//and arc_sized_adjust() are functions defined in libsolaris.so.
size_t (*arc_lowmem_fun)(void *arg, int howto);
size_t (*arc_sized_adjust_fun)(int64_t to_reclaim);

size_t arc_shrinker::request_memory(size_t s, bool hard)
{
    size_t ret = 0;
    if (hard) {
        ret = (*arc_lowmem_fun)(nullptr, 0);
        // ARC's aggressive mode will call arc_adjust, which will reduce the size of the
        // cache, but won't necessarily free as much memory as we need. If it doesn't,
        // keep going in soft mode. This is better than calling arc_lowmem() again, since
        // that could reduce the cache size even further.
        if (ret >= s) {
            return ret;
        }
    }

    // In many situations a soft shrink may only mean "free something".  If we
    // were given a big value fine, try to achieve it. Otherwise go for a
    // minimum of 16 M.
    s = std::max(s, (16ul << 20));
    do {
        size_t r = (*arc_sized_adjust_fun)(s);
        if (r == 0) {
            break;
        }
        ret += r;
    } while (ret < s);
    return ret;
}

void bsd_shrinker_init(void)
{
    struct eventhandler_list *list = eventhandler_find_list("vm_lowmem");
    struct eventhandler_entry *ep;

    debugf("BSD shrinker: event handler list found: %p\n", list);

    TAILQ_FOREACH(ep, &list->el_entries, ee_link) {
        debugf("\tBSD shrinker found: %p\n",
                ((struct eventhandler_entry_generic *)ep)->func);

        auto *_ee = (struct eventhandler_entry_generic *)ep;

        if ((void *)_ee->func == (void *)arc_lowmem_fun) {
            new arc_shrinker();
        } else {
            new bsd_shrinker(_ee);
        }
    }
    EHL_UNLOCK(list);

    debug("BSD shrinker: unlocked, running\n");
}

//This needs to be a C-style function so it can be called
//from libsolaris.so
extern "C" OSV_LIBSOLARIS_API void register_shrinker_arc_funs(
    size_t (*_arc_lowmem_fun)(void *, int),
    size_t (*_arc_sized_adjust_fun)(int64_t)) {
    arc_lowmem_fun = _arc_lowmem_fun;
    arc_sized_adjust_fun = _arc_sized_adjust_fun;
}
