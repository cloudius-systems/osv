#include "osv/trace.hh"
#include "sched.hh"
#include "arch.hh"
#include <atomic>
#include <regex>
#include <boost/algorithm/string/replace.hpp>
#include <debug.hh>

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
constexpr unsigned max_trace = trace_page_size * 1024;

char trace_log[max_trace] __attribute__((may_alias, aligned(sizeof(long))));
std::atomic<size_t> trace_record_last;
bool trace_enabled;

typeof(tracepoint_base::tp_list) tracepoint_base::tp_list __attribute__((init_priority(4000)));

std::vector<std::regex> enabled_tracepoint_regexs;

void enable_trace()
{
    trace_enabled = true;
}

void enable_tracepoint(std::string wildcard)
{
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
    tp_list.push_back(*this);
    try_enable();
}

tracepoint_base::~tracepoint_base()
{
    tp_list.erase(tp_list.iterator_to(*this));
    known_ids().erase(id);
}

void tracepoint_base::enable()
{
    if (enabled) {
        return;
    }
    enabled = true;
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

void tracepoint_base::try_enable()
{
    for (auto& re : enabled_tracepoint_regexs) {
        if (std::regex_match(std::string(name), re)) {
            enable();
        }
    }
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

trace_record* allocate_trace_record(size_t size)
{
    size += sizeof(trace_record);
    size = align_up(size, sizeof(long));
    size_t p = trace_record_last.load(std::memory_order_relaxed);
    size_t pn;
    do {
        pn = p + size;
        if (align_down(p, trace_page_size) != align_down(pn, trace_page_size)) {
            // crossed page boundary
            pn = align_up(p, trace_page_size) + size;
        }
    } while (!trace_record_last.compare_exchange_weak(p, pn, std::memory_order_relaxed));
    char* pp = &trace_log[p % max_trace];
    // clear the first word, do indicate an padding at the end of the page
    reinterpret_cast<trace_record*>(pp)->tp = nullptr;
    pn -= size;
    return reinterpret_cast<trace_record*>(&trace_log[pn % max_trace]);
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

