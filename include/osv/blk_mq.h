/*
 * Copyright (C) 2026 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_BLK_MQ_H
#define OSV_BLK_MQ_H

#include <osv/bio.h>
#include <osv/mutex.h>
#include <vector>
#include <atomic>

__BEGIN_DECLS

struct blk_mq_hw_ctx {
    unsigned int queue_num;
    void* driver_data;
    std::atomic<unsigned int> nr_active;
    mutex lock;
};

struct blk_mq_tag_set;

struct blk_mq_ops {
    int (*queue_rq)(struct blk_mq_hw_ctx* hctx, struct bio* bio);
    void (*complete)(struct bio* bio);
};

struct blk_mq_tag_set {
    const struct blk_mq_ops* ops;
    unsigned int nr_hw_queues;
    unsigned int queue_depth;
    void* driver_data;
    std::vector<struct blk_mq_hw_ctx*> queue_map;
};

int blk_mq_init_tag_set(struct blk_mq_tag_set* set);
void blk_mq_free_tag_set(struct blk_mq_tag_set* set);

struct blk_mq_hw_ctx* blk_mq_get_hctx(struct blk_mq_tag_set* set);
int blk_mq_submit_bio(struct blk_mq_tag_set* set, struct bio* bio);

__END_DECLS

#endif
