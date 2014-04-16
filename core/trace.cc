/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "osv/trace.hh"
#include <osv/sched.hh>
#include "arch.hh"
#include <atomic>
#include <regex>
#include <boost/algorithm/string/replace.hpp>
#include <boost/range/algorithm/remove.hpp>
#include <osv/debug.hh>
#include <osv/prio.hh>
#include <osv/execinfo.hh>
#include <osv/percpu.hh>
#include <osv/ilog2.hh>

using namespace std;

tracepoint<1, void*, void*> trace_function_entry("function entry", "fn %p caller %p");
tracepoint<2, void*, void*> trace_function_exit("function exit", "fn %p caller %p");

// since tracepoint<> is in an anonymous namespace, the compiler eliminates
// global symbols that refer to objects of that type.
//
// Add another reference so the debugger can access trace_function_entry
tracepoint_base* gdb_trace_function_entry = &trace_function_entry;
tracepoint_base* gdb_trace_function_exit = &trace_function_exit;

struct tracepoint_patch_site {
    tracepoint_id id;
    void* patch_site;
    void* slow_path;
};

extern "C" tracepoint_patch_site
    __tracepoint_patch_sites_start[], __tracepoint_patch_sites_end[];

struct tracepoint_patch_sites_type {
    tracepoint_patch_site* begin() { return __tracepoint_patch_sites_start; }
    tracepoint_patch_site* end() { return __tracepoint_patch_sites_end; }
};

tracepoint_patch_sites_type tracepoint_patch_sites;

constexpr size_t trace_page_size = 4096;  // need not match arch page size

// Having a struct is more complex than it need be for just per-vcpu buffers,
// _but_ it is in line with later on having rotating buffers, thus wwhy not do it already
struct trace_buf {
    std::unique_ptr<char[]>
           _base;
    size_t _last;
    size_t _size;

    trace_buf() :
            _base(nullptr), _last(0), _size(0) {
    }
    trace_buf(size_t size) :
            _base(static_cast<char*>(aligned_alloc(sizeof(long), size))), _last(
                    0), _size(size) {
        static_assert(is_power_of_two(trace_page_size), "just checking");
        assert(is_power_of_two(size) && "size must be power of two");
        assert((size & (trace_page_size - 1)) == 0 && "size must be multiple of trace_page_size");
        // Q: should the above be a throw?
        bzero(_base.get(), _size);
    }
    trace_buf(const trace_buf&) = delete;
    trace_buf(trace_buf && buf) = default;

    trace_buf & operator=(const trace_buf&) = delete;
    trace_buf & operator=(trace_buf && buf) = default;

    trace_record * allocate_trace_record(size_t size) {
        size += sizeof(trace_record);
        size = align_up(size, sizeof(long));
        assert(size <= trace_page_size);
        size_t p = _last;
        size_t pn = p + size;
        if (align_down(p, trace_page_size) != align_down(pn - 1, trace_page_size)) {
            // crossed page boundary
            pn = align_up(p, trace_page_size) + size;
        }
        auto * tr0 = reinterpret_cast<trace_record*>(&_base.get()[index(p)]);
        auto * tr1 = reinterpret_cast<trace_record*>(&_base.get()[index(pn - size)]);
        // Put an "end-marker" on the record being written to signify this is yet incomplete.
        // Reader is only this vcpu or attached debugger -> no fence needed.
        tr1->tp = reinterpret_cast<tracepoint_base *>(-1);
        if (tr0 != tr1) {
            // clear the prev word, do indicate padding at the end of the page
            tr0->tp = nullptr;
        }
        barrier();
        _last = pn;
        return tr1;

    }
private:
    inline size_t index(size_t s) const {
        return s & (_size - 1);
    }
};

PERCPU(trace_buf, percpu_trace_buffer);
bool trace_enabled;

typeof(tracepoint_base::tp_list) tracepoint_base::tp_list __attribute__((init_priority((int)init_prio::tracepoint_base)));

std::vector<std::regex> enabled_tracepoint_regexs;

void enable_trace()
{
    trace_enabled = true;
}

void enable_tracepoint(std::string wildcard)
{
    static bool buffers_initialized;

    if (!buffers_initialized) {
        // Ensure we're using power of two sizes * trace_page_size, so round num cpus to ^2
        const size_t ncpu = 1 << size_t(ilog2_roundup(sched::cpus.size()));
        // TODO: examine these defaults. I'm guessing less than 256*mt sized buffers
        // will be subpar, so even if it bloats us on >4 vcpu lets do this for now.
        const size_t size = trace_page_size * std::max(size_t(256), 1024 / ncpu);
        for (auto c : sched::cpus) {
            auto * tbp = percpu_trace_buffer.for_cpu(c);
            *tbp = trace_buf(size);
        }
        buffers_initialized = true;
    }
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("*"), std::string(".*"));
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("?"), std::string("."));
    std::regex re{wildcard};
    enabled_tracepoint_regexs.push_back(re);
    for (auto& tp : tracepoint_base::tp_list) {
        if (std::regex_match(std::string(tp.name), re)) {
            tp.enable();
        }
    }
}

namespace std {

template<>
struct hash<tracepoint_id> {
    size_t operator()(const tracepoint_id& id) const {
        std::hash<const std::type_info*> h0;
        std::hash<unsigned> h1;
        return h0(std::get<0>(id)) ^ h1(std::get<1>(id));
    }
};

}

tracepoint_base::tracepoint_base(unsigned _id, const std::type_info& tp_type,
                                 const char* _name, const char* _format)
    : id{&tp_type, _id}, name(_name), format(_format)
{
    auto inserted = known_ids().insert(id).second;
    if (!inserted) {
        debug("duplicate tracepoint id %d (%s)\n", std::get<0>(id), name);
        abort();
    }
    probes_ptr.assign(new std::vector<probe*>);
    tp_list.push_back(*this);
    try_enable();
}

tracepoint_base::~tracepoint_base()
{
    tp_list.erase(tp_list.iterator_to(*this));
    known_ids().erase(id);
    delete probes_ptr.read();
}

void tracepoint_base::enable()
{
    logging = true;
    update();
}

void tracepoint_base::update()
{
    bool empty;

    WITH_LOCK(osv::rcu_read_lock) {
        auto& probes = *probes_ptr.read();

        empty = probes.empty();
    }

    bool new_active = logging || !empty;
    if (new_active && !active) {
        activate();
    } else if (!new_active && active) {
        deactivate();
    }
}

void tracepoint_base::activate()
{
    active = true;
    for (auto& tps : tracepoint_patch_sites) {
        if (id == tps.id) {
            auto dst = static_cast<char*>(tps.slow_path);
            auto src = static_cast<char*>(tps.patch_site) + 5;
            // FIXME: can fail on smp.
            *static_cast<u8*>(tps.patch_site) = 0xe9; // jmp
            *static_cast<u32*>(tps.patch_site + 1) = dst - src;
        }
    }
}

void tracepoint_base::deactivate()
{
    active = false;
    for (auto& tps : tracepoint_patch_sites) {
        if (id == tps.id) {
            auto p = static_cast<u8*>(tps.patch_site);
            // FIXME: can fail on smp.
            p[0] = 0x0f;
            p[1] = 0x1f;
            p[2] = 0x44;
            p[3] = 0x00;
            p[4] = 0x00;
        }
    }
}

void tracepoint_base::run_probes() {
    WITH_LOCK(osv::rcu_read_lock) {
        auto &probes = *probes_ptr.read();
        for (auto probe : probes) {
            probe->hit();
        }
    }
}

void tracepoint_base::try_enable()
{
    for (auto& re : enabled_tracepoint_regexs) {
        if (std::regex_match(std::string(name), re)) {
            enable();
        }
    }
}

void tracepoint_base::add_probe(probe* p)
{
    WITH_LOCK(probes_mutex) {
        auto old = probes_ptr.read_by_owner();
        auto _new = new std::vector<probe*>(*old);
        _new->push_back(p);
        probes_ptr.assign(_new);
        osv::rcu_dispose(old);
    }
    update();
}

void tracepoint_base::del_probe(probe* p)
{
    WITH_LOCK(probes_mutex) {
        auto old = probes_ptr.read_by_owner();
        auto _new = new std::vector<probe*>(*old);
        auto i = boost::remove(*_new, p);
        _new->erase(i, _new->end());
        probes_ptr.assign(_new);
        osv::rcu_dispose(old);
    }
    osv::rcu_synchronize();
    update();
}

std::unordered_set<tracepoint_id>& tracepoint_base::known_ids()
{
    // since tracepoints are constructed in global scope, use
    // a function static scope which is guaranteed to initialize
    // when needed (as opposed to the unspecified order of global
    // scope initializers)
    static std::unordered_set<tracepoint_id> _known_ids;
    return _known_ids;
}

bool tracepoint_base::_log_backtrace;

void tracepoint_base::log_backtraces()
{
    _log_backtrace = true;
}

void tracepoint_base::do_log_backtrace(trace_record* tr, u8*& buffer)
{
    tr->backtrace = true;
    auto bt = reinterpret_cast<void**>(buffer);
    auto done = backtrace_safe(bt, backtrace_len);
    fill(bt + done, bt + backtrace_len, nullptr);
    buffer += backtrace_len * sizeof(void*);
}

trace_record* allocate_trace_record(size_t size)
{
    return percpu_trace_buffer->allocate_trace_record(size);
}

static __thread unsigned func_trace_nesting;

extern "C" void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    if (!trace_enabled) {
        return;
    }
    arch::irq_flag_notrace irq;
    irq.save();
    arch::irq_disable_notrace();
    if (func_trace_nesting++ == 0) {
        trace_function_entry(this_fn, call_site);
    }
    --func_trace_nesting;
    irq.restore();
}

extern "C" void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
    if (!trace_enabled || !arch::tls_available()) {
        return;
    }
    arch::irq_flag_notrace irq;
    irq.save();
    arch::irq_disable_notrace();
    if (func_trace_nesting++ == 0) {
        trace_function_exit(this_fn, call_site);
    }
    --func_trace_nesting;
    irq.restore();
}

