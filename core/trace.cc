/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "osv/trace.hh"
#include "osv/tracecontrol.hh"
#include <osv/sched.hh>
#include <osv/mutex.h>
#include "arch.hh"
#include <atomic>
#include <regex>
#include <fstream>
#include <unordered_map>
#include <boost/algorithm/string/replace.hpp>
#include <boost/range/algorithm/remove.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <osv/debug.hh>
#include <osv/prio.hh>
#include <osv/execinfo.hh>
#include <osv/percpu.hh>
#include <osv/ilog2.hh>
#include <osv/semaphore.hh>
#include <osv/elf.hh>
#include <cxxabi.h>

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
    std::unique_ptr<char[], decltype(&free)>
           _base;
    size_t _last;
    size_t _size;

    trace_buf() :
            _base(nullptr, free), _last(0), _size(0) {
    }
    trace_buf(size_t size) :
            _base(static_cast<char*>(aligned_alloc(sizeof(long), size)), free), _last(
                    0), _size(size) {
        static_assert(is_power_of_two(trace_page_size), "just checking");
        assert(is_power_of_two(size) && "size must be power of two");
        assert((size & (trace_page_size - 1)) == 0 && "size must be multiple of trace_page_size");
        // Q: should the above be a throw?
        bzero(_base.get(), _size);
    }
    trace_buf(const trace_buf & buf) :
        trace_buf(buf._size)
    {
        memcpy(_base.get(), buf._base.get(), _size);
        _last = buf._last;
    }
    trace_buf(trace_buf && buf) = default;

    trace_buf & operator=(const trace_buf&) = delete;
    trace_buf & operator=(trace_buf && buf) = default;

    static tracepoint_base * const invalid_trace_point;

    size_t last() const {
        return index(_last);
    }

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
        tr1->tp = invalid_trace_point;
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

tracepoint_base * const trace_buf::invalid_trace_point = reinterpret_cast<tracepoint_base *>(-1);

PERCPU(trace_buf, percpu_trace_buffer);
bool trace_enabled;
static bool global_backtrace_enabled;

typeof(tracepoint_base::tp_list) tracepoint_base::tp_list __attribute__((init_priority((int)init_prio::tracepoint_base)));

// Note: the definition of this list is: "expressions from command line",
// and its only use is to deal with late initialization of tp:s
std::vector<std::regex> enabled_tracepoint_regexs;

void enable_trace()
{
    trace_enabled = true;
}

void ensure_log_initialized()
{
    static std::mutex _mutex;
    static std::atomic<bool> buffers_initialized;

    if (buffers_initialized.load(std::memory_order_acquire)) {
        return;
    }

    WITH_LOCK(_mutex) {
        if (buffers_initialized.load(std::memory_order_relaxed)) {
            return;
        }

        // Ensure we're using power of two sizes * trace_page_size, so round num cpus to ^2
        const size_t ncpu = 1 << size_t(ilog2_roundup(sched::cpus.size()));
        // TODO: examine these defaults. I'm guessing less than 256*mt sized buffers
        // will be subpar, so even if it bloats us on >4 vcpu lets do this for now.
        const size_t size = trace_page_size * std::max(size_t(256), 1024 / ncpu);
        for (auto c : sched::cpus) {
            auto * tbp = percpu_trace_buffer.for_cpu(c);
            *tbp = trace_buf(size);
        }

        buffers_initialized.store(true, std::memory_order_release);
    }
}

void enable_tracepoint(std::string wildcard)
{
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("*"), std::string(".*"));
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("?"), std::string("."));
    std::regex re(wildcard);
    trace::set_event_state(re, true);
    enabled_tracepoint_regexs.push_back(re);
}

void enable_backtraces(bool backtrace) {
    global_backtrace_enabled = backtrace;
    for (auto& tp : tracepoint_base::tp_list) {
        tp.backtrace(backtrace);
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

static lockfree::mutex trace_control_lock;

void tracepoint_base::enable(bool enable)
{
    if (enable) {
        ensure_log_initialized();
    }
    // Need lock around this since "update" is a 1+ process
    WITH_LOCK(trace_control_lock) {
        _logging = enable;
        update();
    }
}

void tracepoint_base::backtrace(bool enable)
{
    _backtrace = enable;
}

void tracepoint_base::update()
{
    // take this here as well, just to ensure we're
    // synced with _logging being updated when coming here from
    // "add_probe" et al.
    WITH_LOCK(trace_control_lock) {
        bool empty;

#if CONF_lazy_stack_invariant
        assert(arch::irq_enabled());
        assert(sched::preemptable());
#endif
#if CONF_lazy_stack
        arch::ensure_next_stack_page();
#endif
        WITH_LOCK(osv::rcu_read_lock) {
            auto& probes = *probes_ptr.read();

            empty = probes.empty();
        }

        bool new_active = _logging || !empty;
        if (new_active && !active) {
            activate();
        } else if (!new_active && active) {
            deactivate();
        }
    }
}

void tracepoint_base::activate()
{
    active = true;
    for (auto& tps : tracepoint_patch_sites) {
        if (id == tps.id) {
            activate(id, tps.patch_site, tps.slow_path);
        }
    }
}

void tracepoint_base::deactivate()
{
    active = false;
    for (auto& tps : tracepoint_patch_sites) {
        if (id == tps.id) {
            deactivate(id, tps.patch_site, tps.slow_path);
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
            // keep the same semantics for command line enabled
            // tp:s as before individually controlled points.
            backtrace(global_backtrace_enabled);
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

void tracepoint_base::do_log_backtrace(trace_record* tr, u8*& buffer)
{
    assert(tr->backtrace);
    auto bt = reinterpret_cast<void**>(buffer);
    auto done = backtrace_safe(bt, backtrace_len);
    fill(bt + done, bt + backtrace_len, nullptr);
    buffer += backtrace_len * sizeof(void*);
}

trace_record* tracepoint_base::allocate_trace_record(size_t size)
{
    const bool bt = _backtrace;
    if (bt) {
        size += backtrace_len * sizeof(void*);
    }
    auto * tr = percpu_trace_buffer->allocate_trace_record(size);
    tr->backtrace = bt;
    tr->thread = sched::thread::current();
    tr->thread_name = tr->thread->name_raw();
    tr->time = 0;
    tr->cpu = -1;
    if (tr->thread) {
        tr->time = clock::get()->uptime();
        tr->cpu = tr->thread->tcpu()->id;
    }
    return tr;
}

static __thread unsigned func_trace_nesting;

extern "C" void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    if (!trace_enabled) {
        return;
    }
    arch::irq_flag_notrace irq;
    irq.save();
#if CONF_lazy_stack
    if (sched::preemptable() && irq.enabled()) {
        arch::ensure_next_stack_page();
    }
#endif
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
#if CONF_lazy_stack
    if (sched::preemptable() && irq.enabled()) {
        arch::ensure_next_stack_page();
    }
#endif
    arch::irq_disable_notrace();
    if (func_trace_nesting++ == 0) {
        trace_function_exit(this_fn, call_site);
    }
    --func_trace_nesting;
    irq.restore();
}

trace::event_info::event_info(const tracepoint_base & tp)
    : id(tp.name)
    , name(tp.name) // TODO: human friendly? desc?
    , enabled(tp.enabled())
    , backtrace(tp.backtrace())
{}

std::vector<trace::event_info>
trace::get_event_info()
{
    std::vector<event_info> res;

    WITH_LOCK(trace_control_lock) {
        std::copy(tracepoint_base::tp_list.begin()
                , tracepoint_base::tp_list.end()
                , std::back_inserter(res)
        );
    }

    return res;
}

std::vector<trace::event_info>
trace::get_event_info(const std::regex & ex)
{
    std::vector<event_info> res;

    WITH_LOCK(trace_control_lock) {
        for (auto & tp : tracepoint_base::tp_list) {
            if (std::regex_match(std::string(tp.name), ex)) {
                res.emplace_back(tp);
            }
        }
    }

    return res;
}

std::vector<trace::event_info>
trace::set_event_state(const std::regex & ex, bool enable, bool backtrace) {
    std::vector<event_info> res;

    // Note: expressions sent here are only treated as instantaneous requests.
    // unlike command line, which is "persisted" and queried on a late tp init.
    WITH_LOCK(trace_control_lock) {
        for (auto & tp : tracepoint_base::tp_list) {
            if (std::regex_match(std::string(tp.name), ex)) {
                res.emplace_back(tp);
                tp.enable(enable);
                tp.backtrace(backtrace);
            }
        }
    }

    return res;
}

trace::event_info
trace::get_event_info(const ext_id & id)
{
    // Note: assuming all tracepoints are created at load time
    // -> no locks for the tp_list. If this changes (hello dynamic tp:s)
    // the lock should be embracing this as well.
    for (auto & tp : tracepoint_base::tp_list) {
        if (id == tp.name) {
            WITH_LOCK(trace_control_lock) {
                return event_info(tp);
            }
        }
    }
    throw std::invalid_argument(id);
}

trace::event_info
trace::set_event_state(const ext_id & id, bool enable, bool backtrace)
{
    for (auto & tp : tracepoint_base::tp_list) {
        if (id == tp.name) {
            return set_event_state(tp, enable, backtrace);
        }
    }
    throw std::invalid_argument(id);
}

trace::event_info
trace::set_event_state(tracepoint_base & tp, bool enable, bool backtrace)
{
    WITH_LOCK(trace_control_lock) {
        event_info old(tp);
        tp.enable(enable);
        tp.backtrace(backtrace);
        return old;
    }
}

static std::unordered_map<trace::generator_id, trace::generate_symbol_table_func> symbol_functions;
static std::mutex symbol_func_mutex;
static trace::generator_id symbol_ids;

trace::generator_id
trace::add_symbol_callback(const generate_symbol_table_func & f) {
    WITH_LOCK(symbol_func_mutex) {
        auto id = ++symbol_ids;
        symbol_functions[id] = f;
        return id;
    }
}

void
trace::remove_symbol_callback(generator_id id) {
    WITH_LOCK(symbol_func_mutex) {
        if (symbol_functions.count(id) > 0) {
            symbol_functions.erase(id);
        }
    }
}

// Helper type to build trace dump binary files
class trace_out: public std::ofstream {
public:
    std::string path;

    trace_out() {
        for (;;) {
            std::unique_ptr<char> tmp(::tempnam(nullptr, nullptr));
            if (tmp) {
                auto f = ::open(tmp.get(), O_EXCL | O_CREAT);
                if (f != -1) {
                    ofstream::open(tmp.get(), ios::out|ios::binary);
                    path = tmp.get();
                    ::close(f);
                    break;
                }
            }
        }
    }
    trace_out & align(size_t a) {
        while (tellp() & (a - 1)) {
            put(0);
        }
        return *this;
    }
    template<typename T> trace_out & align() {
        return align(std::alignment_of<T>::value);
    }

    using std::ofstream::write;

    template<typename T> trace_out & write(T && t) {
        align<T>();
        write(reinterpret_cast<const char_type*>(&t), sizeof(t));
        return *this;
    }
    template<typename T> trace_out & twrite(const char *& s) {
        const auto a = object_serializer<T>().alignment();
        s = align_up(s, a);
        align(a);
        write(s, sizeof(T));
        s += sizeof(T);
        return *this;
    }
    template<typename T> trace_out & twrite(const char *& s, size_t n) {
        while (n-- > 0) {
            twrite<T>(s);
        }
        return *this;
    }
    trace_out & swrite(const char * s) {
        size_t len = s != nullptr ? strlen(s) : 0;
        write(u16(len));
        write(s, len);
        return *this;
    }
    trace_out & swrite(const std::string & s) {
        write(u16(s.size()));
        write(s.c_str(), s.size());
        return *this;
    }
};

template<typename T = uint32_t>
struct length {
public:
    length(trace_out & out, T v = T()) :
            value(v), _out(out), _pos(out.tellp()) {
        out.write(T());
    }
    ~length() {
        auto p = _out.tellp();
        _out.seekp(_pos);
        _out.write(value);
        _out.seekp(p);
    }
    T value;
private:
    trace_out & _out;
    trace_out::pos_type _pos;
};

/*
  Format (please keep in sync with doc/wiki)

Note: All chunks are aligned on eight (8, sizeof(uint64_t)).
Otherwise data is aligned on their "natural" alignment.
Semi-structs such as string are aligned on the first member.

Chunks are typed with a 32-bit FOURCC tag, written in native
endian, i.e. 'ROCK' on LE becomes the string "KCOR".
Size is a somewhat overzealeous uint64_t value, thus in effect
there is a "reserved" 32-bit value between tag and size.

Any chunk type may appear more than once, and in no particular order,
except that actual trace data chunks should appear last.

string = {
  uint16_t len;
  char data[len];
};

dump = <chunk> {
  uint32_t tag = ‘OSVT’;
  uint64_t size = <dump size>;
  uint32_t format_version;

  trace_dictionary = <chunk, align 8> {
    uint32_t tag = 'TRCD';
    uint64_t size = <cs>;
    uint32_t backtrace_len; // how long are all backtraces
    // array of trace point definitions
    uint32_t n_types;
    struct {
      uint64_t tag;
      string id;
      string name;
      string prov;
      string format;
      // array of arguments
      uint32_t n_args;
      struct {
        string name;
        char type;
      } [n_args];
    } [n_types];
  } +; // one or more, may repeat

  loaded_moduled = <chunk, align 8> {
    uint32_t tag = ‘MODS’;
    uint64_t size = <chunk size>;
    // array of module info
    uint32_t n_modules;
    struct {
      string path;
      uint64_t base;
      // array of loaded segments
      uint32_t n_segments;
      struct {
        string name;
        uint32_t type;
        uint32_t info
        uint64_t flags;
        uint64_t address;
        uint64_t offset;
        uint64_t size;
      } [n_segments];
    } [n_modules];
  } *; // zero or more, may repeat

  symbol_table = <chunk, align 8> {
    uint32_t tag = 'SYMB';
    uint32_t size = <cs>;
    // array of symbol entries
    uint32_t n_symbols;
    struct {
      string name;
      uint64_t address;
      uint64_t size;
      string filename;
      uint32_t n_locations;
      struct {
          uint32_t offset;
          int32_t line;
      } [n_locations];
    } [n_symbols];
  } *; // zero or more, may repeat

  trace_data = <chunk, align 8> {
    uint32_t tag = ‘TRCS’;
    uint64_t size = <chunk size>;
    <align 8>
    //<raw traces, but with gaps removed>
  } +; // 1 or more
};

 */
std::string
trace::create_trace_dump()
{
    semaphore signal(0);
    std::vector<trace_buf> copies(sched::cpus.size());

    auto is_valid_tracepoint = [](const tracepoint_base * tp_test) {
        for (auto & tp : tracepoint_base::tp_list) {
            if (&tp == tp_test) {
                return true;
            }
        }
        return false;
    };

    // Copy the trace buffers from each cpu, locking out trace generation
    // during the extraction (disable preemption, just like trace write)
    unsigned i = 0;
    for (auto & cpu : sched::cpus) {
        std::unique_ptr<sched::thread> t(sched::thread::make([&, i]() {
            arch::irq_flag_notrace irq;
            irq.save();
            arch::irq_disable_notrace();
            auto * tbp = percpu_trace_buffer.for_cpu(cpu);
            copies[i] = trace_buf(*tbp);
            irq.restore();
            signal.post();
        }, sched::thread::attr().pin(cpu)));
        t->start();
        t->join();
        ++i;
    }
    // Redundant. But just to verify.
    signal.wait(sched::cpus.size());

    // Dealing with 'FOUR' fourcc tags
    struct tag {
        tag(const char (&s)[5]) :
            _val((s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3])
        {}
        operator uint32_t() const {
            return _val;
        }
        const uint32_t _val;
    };

    // RIFF-like chunk (see file format description).
    // Always aligned on 8
    class chunk {
    public:
        chunk(trace_out & out, const tag & tt) :
                _out(out) {
            out.align(8);
            out.write(uint32_t(tt));
            out.align(8);
            _pos = out.tellp();
            out.write(uint64_t(0));
        }
        ~chunk() {
            auto p = _out.tellp();
            _out.seekp(_pos);
            _out.write(uint64_t(p - _pos - sizeof(uint64_t)));
            _out.seekp(p);
        }
    private:
        trace_out & _out;
        trace_out::pos_type _pos;
    };

    static const int tf_version_major = 0;
    static const int tf_version_minor = 1;

    trace_out out;

    // Want early fail
    out.exceptions(trace_out::failbit);

    {
        chunk osvt(out, "OSVT"); // magic
        out.write(uint32_t(1)); // endian (verify)
        out.write(uint32_t((tf_version_major << 16) | tf_version_minor)); // version

        // Trace dictionary
        {
            chunk dict(out, "TRCD");

            out.write(uint32_t(tracepoint_base::backtrace_len));
            out.write(uint32_t(tracepoint_base::tp_list.size()));

            for (auto & tp : tracepoint_base::tp_list) {
                out.write(reinterpret_cast<uint64_t>(&tp)); // tag/ptr
                out.swrite(tp.name); // id
                out.swrite(tp.name); // name (TODO: useful names)
                out.swrite("OSv"); // provider
                out.swrite(tp.format); // print format (?)
                out.write<uint32_t>(strlen(tp.sig));
                int n = 0;
                auto s = tp.sig;
                while (*s) {
                    out.swrite(std::to_string(n++)); // no arg names
                    out.write(*s);
                    ++s;
                }
            }
        }

        { // Module list
            elf::get_program()->with_modules(
                    [&](const elf::program::modules_list &ml)
                    {
                        {
                            chunk mods(out, "MODS");
                            out.write(uint32_t(ml.objects.size()));
                            for (auto module : ml.objects) {
                                out.swrite(module->pathname());
                                out.write(uint64_t(module->base()));
                                out.write(uint64_t(module->end()) - uint64_t(module->base()));

                                if (module->module_index() == elf::program::core_module_index) {
                                    out.write(uint32_t(0));
                                    continue;
                                }
                                // Sections
                                auto sections = module->sections();
                                out.write(uint32_t(sections.size()));
                                for (auto & section : sections) {
                                    out.swrite(module->section_name(section));
                                    out.write(uint32_t(section.sh_type));
                                    out.write(uint32_t(section.sh_info));
                                    out.write(uint64_t(section.sh_flags));
                                    out.write(uint64_t(section.sh_addr));
                                    out.write(uint64_t(section.sh_offset));
                                    out.write(uint64_t(section.sh_size));
                                }
                            }
                        }

                        struct demangler {
                            demangler()
                            {}
                            ~demangler()
                            {
                                if (buf) {
                                    free(buf);
                                }
                            }
                            const char * operator()(const char * name) {
                                int status;
                                auto * demangled = abi::__cxa_demangle(name, buf, &len, &status);
                                if (demangled) {
                                    buf = demangled;
                                    return buf;
                                }
                                return name;
                            }
                        private:
                            char * buf = nullptr;
                            size_t len = 0;
                        };

                        demangler demangle;

                        for (auto module : ml.objects) {
                            auto syms = module->symbols();
                            if (syms.empty()) {
                                continue;
                            }
                            chunk mods(out, "SYMB");
                            length<> len(out);
                            for (auto & es : syms) {
                                auto t = es.st_info & elf::STT_HIPROC;
                                if (t != elf::STT_FUNC && t != elf::STT_OBJECT) {
                                    continue;
                                }
                                auto * n = module->symbol_name(&es);
                                if (n && *n) {
                                    elf::symbol_module m(&es, module);
                                    ++len.value;
                                    out.swrite(demangle(n));
                                    out.write(uint64_t(m.relocated_addr()));
                                    out.write(uint64_t(m.size()));
                                    out.swrite(nullptr);
                                    out.write(uint32_t(0));
                                }
                            }

                        }
                    });
        }


        {
            // Symbol tables
            WITH_LOCK(symbol_func_mutex) {
                for (auto & p : symbol_functions) {
                    chunk symb(out, "SYMB");
                    length<> len(out);
                    p.second([&](const symbol & s) {
                        ++len.value;
                        out.swrite(s.name);
                        out.write(uint64_t(s.addr));
                        out.write(uint64_t(s.size));
                        out.swrite(s.filename);
                        out.write(s.n_locations);
                        for (uint32_t i = 0; i < s.n_locations; ++i) {
                            auto loc = s.location(i);
                            out.write(loc.first);
                            out.write(loc.second);
                        }
                    });
                }
            }
        }

        // Trace data, one chunk for each cpu buffer
        for (auto & buf : copies) {
            const auto last = buf.last();
            const auto pivot = align_up(last, trace_page_size);
            const auto regs = { std::make_pair(buf._base.get() + pivot,
                    buf._base.get() + buf._size), std::make_pair(
                    buf._base.get(), buf._base.get() + last) };

            chunk trcs(out, "TRCS");

            out.align(8);

            for (auto & r : regs) {
                const char * s = r.first;
                const char * e = r.second;

                while (s < e) {
                    auto * tr = reinterpret_cast<const trace_record*>(s);
                    if (tr->tp == nullptr) {
                        // alignment up to 8 is fine on the pointer itself.
                        // page alignment we must do per offset.
                        size_t off = s - r.first;
                        s = r.first + align_up(off + 1, trace_page_size);
                        continue;
                    }
                    if (tr->tp == trace_buf::invalid_trace_point) {
                        break;
                    }

                    assert(is_valid_tracepoint(tr->tp));

                    out.twrite<trace_record>(s);

                    if (tr->backtrace) {
                        out.twrite<void *>(s, tracepoint_base::backtrace_len);
                    }
                    auto sig = tr->tp->sig;
                    while (*sig != 0) {
                        switch (*sig++) {
                        case 'c':
                            out.twrite<char>(s);
                            break;
                        case 'b':
                        case 'B':
                            out.twrite<u8>(s);
                            break;
                        case 'h':
                        case 'H':
                            out.twrite<u16>(s);
                            break;
                        case 'i':
                        case 'I':
                        case 'f':
                            out.twrite<u32>(s);
                            break;
                        case 'q':
                        case 'Q':
                        case 'd':
                        case 'P':
                            out.twrite<u64>(s);
                            break;
                        case '?':
                            out.twrite<bool>(s);
                            break;
                        case 'p': {
                            out.twrite<char>(s,
                                    object_serializer<const char*>::max_len);
                            break;
                        }
                        case '*': {
                            s = align_up(s, sizeof(u16));
                            auto len = *reinterpret_cast<const u16*>(s);
                            s += 2;
                            out.write(len);
                            out.twrite<char>(s, len);
                            break;
                        }
                        default:
                            assert(0 && "should not reach");
                        }
                    }
                    s = align_up(s, sizeof(long));
                }
            }
        }

    }
    out.flush();
    out.close();

    return std::move(out.path);
}
