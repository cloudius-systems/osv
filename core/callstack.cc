#include <osv/callstack.hh>
#include <execinfo.h>
#include <stddef.h>
#include <osv/execinfo.hh>

callstack_collector::callstack_collector(size_t nr_traces, unsigned skip_frames, unsigned nr_frames)
    : _nr_traces(nr_traces)
    , _skip_frames(skip_frames)
    , _nr_frames(nr_frames)
    , _buffer(malloc(nr_traces * trace_object_size()))
    , _free_traces(_buffer)
{
    for (auto c : sched::cpus) {
        _buckets.push_back(std::vector<table_type::bucket_type>(nr_traces, table_type::bucket_type()));
        auto& b = _buckets.back();
        _table.for_cpu(c)->reset(new table_type(table_type::bucket_traits(b.data(), b.size())));
    }
}

callstack_collector::~callstack_collector()
{
    free(_buffer);
}

void callstack_collector::attach(tracepoint_base& tp)
{
    _attached.push_back(&tp);
}

size_t callstack_collector::trace_object_size()
{
    return sizeof(trace) + _nr_frames * sizeof(void*);
}

void callstack_collector::start()
{
    for (auto tp : _attached) {
        tp->add_probe(this);
    }
    _running.store(true);
}

void callstack_collector::stop()
{
    _running.store(false);
    for (auto tp : _attached) {
        tp->del_probe(this);
    }
    // move callstacks from per-cpu hash tables to cpu 0.
    merge();
}

// move all traces to cpu0's table
void callstack_collector::merge()
{
    auto& table0 = **_table.for_cpu(sched::cpus[0]);
    for (auto c : sched::cpus) {
        if (c == sched::cpus[0]) {
            continue;
        }
        auto& table = **_table.for_cpu(c);
        for (auto& tr : table) {
            auto i = table0.find(tr);
            if (i != table0.end()) {
                i->hits += tr.hits;
            } else {
                table.erase(table.iterator_to(tr));
                table0.insert(tr);
            }
        }
    }
}

bool callstack_collector::histogram_compare::operator()(trace* a, trace* b)
{
    if (a->hits > b->hits) {
        return true;
    } else if (a->hits < b->hits) {
        return false;
    } else {
        return a < b;
    }
}

inline bool operator==(const callstack_collector::trace& a,
                       const callstack_collector::trace& b)
{
    return a.len == b.len && std::equal(a.pc, a.pc + a.len, b.pc);
}

inline bool bt_trace_compare(void** bt, const callstack_collector::trace& b)
{
    return std::equal(bt, bt + b.len, b.pc);
}

struct backtrace_hash {
    backtrace_hash(unsigned nr) : nr(nr) {}
    size_t operator()(void* const * bt) const {
        size_t r = 0;
        std::hash<const void*> hashfn;
        for (unsigned i = 0; i < nr; ++i) {
            r = (r << 7) | (r >> (sizeof(r)*8 - 7));
            r ^= hashfn(bt[i]);
        }
        return r;
    }
    unsigned nr;
};

size_t hash_value(const callstack_collector::trace& a)
{
    backtrace_hash hash(a.len);
    return hash(a.pc);
}

callstack_collector::trace* callstack_collector::alloc_trace(void** pc, unsigned len)
{
    auto t = _free_traces.fetch_add(trace_object_size(), std::memory_order_relaxed);
    return new (t) trace(pc, len);
}

callstack_collector::trace::trace(void** pc, unsigned len)
    : hits()
    , len(len)
{
    std::copy(pc, pc + len, this->pc);
}

// an instrumented tracepoint was hit; collect a trace
void callstack_collector::hit()
{
    void* bt0[100];
    void** bt = bt0;
    int nr = backtrace_safe(bt, std::min(100u, _skip_frames + _nr_frames));
    bt += _skip_frames;
    nr -= _skip_frames;
    auto table = _table->get();
    backtrace_hash hash(nr);
    auto i = table->find(bt, hash, bt_trace_compare);
    if (i == table->end()) {
        // new unique trace, copy and store it
        auto t = alloc_trace(bt, nr);
        if (!t) {
            _overflow.store(true);
            return;
        }
        i = table->insert(*t).first;
    }
    ++i->hits;
}

auto callstack_collector::histogram(size_t n) -> std::set<trace*, histogram_compare>
{
    std::set<trace*, callstack_collector::histogram_compare> h;
    for (auto c : sched::cpus) {
        for (auto& tr : **_table.for_cpu(c)) {
            h.insert(&tr);
            // drop off least frequent element, if we already have enough
            if (h.size() > n) {
                h.erase(std::prev(h.end()));
            }
        }
    }
    return h;
}
