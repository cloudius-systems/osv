/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_THREAD_STATE_HH_
#define ARCH_THREAD_STATE_HH_

struct thread_state {
    void* fp;
    void* thread;

    void* sp;
    void* pc;
};

#endif /* ARCH_THREAD_STATE_HH_ */
