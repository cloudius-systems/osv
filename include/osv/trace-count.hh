/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#ifndef INCLUDED_TRACE_COUNT_HH
#define INCLUDED_TRACE_COUNT_HH

#include <osv/per-cpu-counter.hh>
#include <osv/trace.hh>

class tracepoint_counter : public tracepoint_base::probe {
public:
    explicit tracepoint_counter(tracepoint_base& tp);
    virtual ~tracepoint_counter();
    virtual void hit();
    ulong read();
private:
    tracepoint_base& _tp;
    per_cpu_counter _counter;
};
#endif /* INCLUDED_TRACE_COUNT_HH */
