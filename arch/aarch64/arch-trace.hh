/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_TRACE_HH_
#define ARCH_TRACE_HH_

#include "osv/trace.hh"

template<unsigned _id, typename ... s_args, typename ... r_args, std::tuple<
        s_args...> (*assign)(r_args...)>
inline void tracepointv<_id, std::tuple<s_args...>(r_args...), assign>::operator()(
        r_args ... as) {
    // TODO: "fast" path
    if (active) {
        trace_slow_path(assign(as...));
    }
}

#endif /* ARCH_TRACE_HH_ */
