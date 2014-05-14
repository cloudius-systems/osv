/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_SAMPLER_HH
#define _OSV_SAMPLER_HH

#include <osv/clock.hh>

namespace prof {

struct config {
    osv::clock::uptime::duration period;
};

/**
 * Starts the sampler.
 *
 * Returns true if sampler was started with the new config, false otherwise.
 * If sampler was already running, false is returned.
 *
 * May block.
 */
bool start_sampler(config) throw();

/**
 * Stops the sampler.
 *
 * May block.
 */
void stop_sampler() throw();

}

#endif
