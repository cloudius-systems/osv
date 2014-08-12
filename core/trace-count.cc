/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/trace-count.hh>

tracepoint_counter::tracepoint_counter(tracepoint_base& tp)
    : _tp(tp)
{
    _tp.add_probe(this);
}

tracepoint_counter::~tracepoint_counter() {
    _tp.del_probe(this);
}

// It is important that we define the hit() function here, in a .cc source
// file part of the OSv kernel, and not in the header file. Otherwise, if a
// shared object (like httpserver.so) uses tracepoint_counter, this function
// would end up in the shared object, which can be paged out and then we
// crash when we reach a tracepoint with preemption disabled.
void tracepoint_counter::hit()
{
    _counter.increment();
}

ulong tracepoint_counter::read()
{
    return _counter.read();
}
