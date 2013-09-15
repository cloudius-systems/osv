/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CALLSTACK_HH_
#define CALLSTACK_HH_

#include <boost/intrusive/unordered_set.hpp>
#include <osv/trace.hh>
#include <osv/percpu.hh>
#include <memory>
#include <atomic>
#include <stdlib.h>
#include <set>

// An object that instruments tracepoints to collect backtraces.
//
// Use:
//   Create the callstack_collector
//   call attach() to instrument interesting tracepoints
//   call start()/stop() to collect samples
//   call dump() to read and process the gathered information
//   destroy the callstack_collector
class callstack_collector : private tracepoint_base::probe {
public:
    // Create a collector that can hold up to @nr_traces distinct callstacks,
    // Each trace will have at most @nr_frames frames; the topmost @skip_frames
    // frames will be skipped.
    callstack_collector(size_t nr_traces, unsigned skip_frames, unsigned nr_frames);
    ~callstack_collector();
    // instrument a tracepoint; must be called before start()
    void attach(tracepoint_base& tp);
    // start collecting samples
    void start();
    // stop collecting samples
    void stop();
    // on a stopped collector, call @func(const trace& tr) for n most
    // common traces; must not take address of @tr.
    template <typename function>
    void dump(size_t n, function func);
public:
    // A representation of a call trace.  Note this is not a standard object
    // as the program counter array is variable length, so it can't be allocated
    // normally or copied.
    struct trace : boost::intrusive::unordered_set_base_hook<> {
        trace(void** pc, unsigned len);
        unsigned hits;  // number of times this trace was seen
        unsigned len;   // length of pc[] array
        void* pc[];     // program counters, most recent first

        trace(const trace&) = delete;
        void operator=(const trace&) = delete;
    };
private:
    // Compares two traces for the histogram (most hits first)
    struct histogram_compare {
        bool operator()(trace* a, trace* b);
    };
private:
    // Callback from tracepoint_base::probe
    virtual void hit() override;
    size_t trace_object_size();
    trace* alloc_trace(void** pc, unsigned len);
    // merge per-cpu traces into cpu0
    void merge();
    std::set<trace*, histogram_compare> histogram(size_t n);
private:
    // non-allocating hash table
    typedef boost::intrusive::unordered_set<trace,
                                            boost::intrusive::constant_time_size<false>> table_type;
    std::atomic<bool> _running = { false };
    std::atomic<bool> _overflow = { false };
    size_t _nr_traces;
    unsigned _skip_frames;
    unsigned _nr_frames;
    // memory buffer for trace objects; since they're variable sized, we can't just
    // use new[].
    void* _buffer;
    // per-cpu bucket vector for for boost::intrusive::unordered_set<>
    std::vector<std::vector<table_type::bucket_type>> _buckets;
    // per-cpu hash table
    dynamic_percpu<std::unique_ptr<table_type>> _table;
    // global pool of free trace objects
    std::atomic<void*> _free_traces;
    // attached tracepoints
    std::vector<tracepoint_base*> _attached;
    friend bool operator==(const trace& a, const trace& b);
    friend bool bt_trace_compare(void** bt, const trace& b);
    friend size_t hash_value(const callstack_collector::trace& a);
};

template <typename function>
void callstack_collector::dump(size_t n, function func)
{
    for (auto tr : histogram(n)) {
        func(*tr);
    }
}

#endif /* CALLSTACK_HH_ */
