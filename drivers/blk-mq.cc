/*
 * Copyright (C) 2026 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/blk_mq.h>
#include <osv/sched.hh>
#include <osv/debug.h>
#include <vector>
#include <errno.h>

int blk_mq_init_tag_set(struct blk_mq_tag_set* set)
{
    if (!set || !set->ops || set->nr_hw_queues == 0) {
        return -EINVAL;
    }

    set->queue_map.resize(set->nr_hw_queues);

    for (unsigned int i = 0; i < set->nr_hw_queues; i++) {
        auto* hctx = new blk_mq_hw_ctx;
        hctx->queue_num = i;
        hctx->driver_data = set->driver_data;
        hctx->nr_active = 0;
        set->queue_map[i] = hctx;
    }

    kprintf("blk_mq: initialized %u hardware queues\n", set->nr_hw_queues);
    return 0;
}

void blk_mq_free_tag_set(struct blk_mq_tag_set* set)
{
    if (!set) {
        return;
    }

    for (auto* hctx : set->queue_map) {
        delete hctx;
    }
    set->queue_map.clear();
}

struct blk_mq_hw_ctx* blk_mq_get_hctx(struct blk_mq_tag_set* set)
{
    if (!set || set->nr_hw_queues == 0) {
        return nullptr;
    }

    unsigned int cpu_id = sched::cpu::current()->id;
    unsigned int queue_idx = cpu_id % set->nr_hw_queues;

    return set->queue_map[queue_idx];
}

int blk_mq_submit_bio(struct blk_mq_tag_set* set, struct bio* bio)
{
    if (!set || !bio) {
        return -EINVAL;
    }

    auto* hctx = blk_mq_get_hctx(set);
    if (!hctx) {
        return -EINVAL;
    }

    hctx->nr_active++;

    int ret = set->ops->queue_rq(hctx, bio);

    if (ret != 0) {
        hctx->nr_active--;
    }

    return ret;
}
