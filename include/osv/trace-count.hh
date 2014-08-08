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
    explicit tracepoint_counter(tracepoint_base& tp) : _tp(tp) {
        // A tracepoint probe's hit() must not call functions which might
        // trigger other tracepoints or preemption. In particular, hit() must
        // not call a previously uncalled function, because when
        // tracepoint_counter is used in a shared object, the first call
        // will trigger a call to program::resolve() which, among other
        // things, allocate memory. So we need to make sure we call
        // per_cpu_count::increment() before setting a probe which uses it.
        {
            per_cpu_counter junk;
            junk.increment();
        }
        _tp.add_probe(this);
    }
    virtual ~tracepoint_counter() { _tp.del_probe(this); }
    virtual void hit() { _counter.increment(); }
    ulong read() { return _counter.read(); }
private:
    tracepoint_base& _tp;
    per_cpu_counter _counter;
};
#endif /* INCLUDED_TRACE_COUNT_HH */
