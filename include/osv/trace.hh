/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef TRACE_HH_
#define TRACE_HH_

#include <iostream>
#include <iterator>
#include <tuple>
#include <boost/format.hpp>
#include <osv/types.h>
#include <osv/align.hh>
#include <osv/sched.hh>
#include <boost/intrusive/list.hpp>
#include <string>
#include <unordered_set>
#include <drivers/clock.hh>
#include <cstring>
#include <arch.hh>
#include <osv/rcu.hh>
#include <safe-ptr.hh>
#include <stdint.h>
#include <atomic>

void enable_trace();
void enable_tracepoint(std::string wildcard);
void enable_backtraces(bool = true);
void list_all_tracepoints();

class tracepoint_base;

struct blob_tag {};

template<typename T>
using is_blob = std::is_base_of<blob_tag, T>;

struct trace_record {
    tracepoint_base* tp;
    sched::thread* thread;
    std::array<char, 16> thread_name;
    u64 time;
    unsigned cpu;
    bool backtrace : 1;  // 10-element backtrace precedes parameters
    union {
        u8 buffer[0];
        long align[0];
    };
};

//Simple lock-less multiple-producer single-consumer structure
//designed to act as a data gateway between threads generating trace
//records and the strace thread printing them to the console
//
//In essence it is an array of pointers to the trace records
//indexed by write_offset which stores next entry offset to write and
//is atomic to guarantee no two producers step on each other and
//read_offset that stores offset of next entry to read
//
//The trace_log is designed as a circular buffer where
//both read and write offsets would wrap around from 0xffff to 0
//so it is possible that with huge number of trace record written
//and slow consumer some data may get overwritten
constexpr const size_t trace_log_size = 0x10000;
struct trace_log {
    trace_record *traces[trace_log_size];
    std::atomic<uint16_t> write_offset = {0};
    uint16_t read_offset = {0};

    void write(trace_record* tr) {
        traces[write_offset.fetch_add(1)] = tr;
    }

    trace_record* read() {
        if (read_offset == write_offset.load()) {
            return nullptr;
        } else {
            return traces[read_offset++];
        }
    }
};

extern trace_log* _trace_log;

template <size_t idx, size_t N, typename... args>
struct tuple_formatter
{
    static boost::format& format(boost::format& fmt, std::tuple<args...> as) {
        typedef tuple_formatter<idx + 1, N, args...> recurse;
        return recurse::format(fmt % std::get<idx>(as), as);
    }
};

template <size_t N, typename... args>
struct tuple_formatter<N, N, args...>
{
    static boost::format& format(boost::format& fmt, std::tuple<args...> as) {
        return fmt;
    }
};

template <typename... args>
inline
boost::format& format_tuple(boost::format& fmt, std::tuple<args...> as)
{
    return tuple_formatter<size_t(0), sizeof...(args), args...>::format(fmt, as);
}

template <typename... args>
inline
boost::format format_tuple(const char* fmt, std::tuple<args...> as)
{
    boost::format format(fmt);
    return format_tuple(format, as);
}

template <typename T, typename = void>
struct signature_char;

template <>
struct signature_char<char> {
    static const char sig = 'c';
};

template <>
struct signature_char<s8> {
    static const char sig = 'b';
};

template <>
struct signature_char<u8> {
    static const char sig = 'B';
};

template <>
struct signature_char<s16> {
    static const char sig = 'h';
};

template <>
struct signature_char<u16> {
    static const char sig = 'H';
};

template <>
struct signature_char<s32> {
    static const char sig = 'i';
};

template <>
struct signature_char<u32> {
    static const char sig = 'I';
};

template <>
struct signature_char<s64> {
    static const char sig = 'q';
};

template <>
struct signature_char<u64> {
    static const char sig = 'Q';
};

template <>
struct signature_char<bool> {
    static const char sig = '?';
};

template <>
struct signature_char<float> {
    static const char sig = 'f';
};

template <>
struct signature_char<double> {
    static const char sig = 'd';
};

template <typename T>
struct signature_char<T*> {
    static const char sig = 'P';
};

template <>
struct signature_char<const char*> {
    static const char sig = 'p';  // "pascal string"
};

template <typename T>
struct signature_char<T, typename std::enable_if<is_blob<T>::value>::type> {
    static const char sig = '*';
};

template <typename... Args>
struct signature_helper {
    static constexpr const char sig[] = { signature_char<Args>::sig..., '\0'};
};

template <typename... Args>
constexpr const char signature_helper<Args...>::sig[];

template <typename, typename = void>
struct object_serializer;

template <typename arg>
struct object_serializer<arg, typename std::enable_if<!is_blob<arg>::value>::type> {
    void serialize(arg val, void* buffer) { *static_cast<arg*>(buffer) = val; }
    size_t size(arg val) { return sizeof(arg); }
    size_t alignment() { return std::min(sizeof(arg), sizeof(long)); } // FIXME: want to use alignof here
};

template <typename T>
struct object_serializer<T, typename std::enable_if<is_blob<T>::value>::type> {
    using len_t = u16;
    typedef typename std::iterator_traits<typename T::iterator>::value_type value_type;
    static_assert(sizeof(value_type) == 1, "value must be one byte long");

    [[gnu::always_inline]]
    void serialize(T range, void* _buffer) {
        auto data_buf = reinterpret_cast<value_type*>(_buffer + sizeof(len_t));
        size_t count = 0;
        for (auto& item : range) {
            *data_buf++ = item;
            count++;
        }
        assert(count <= std::numeric_limits<len_t>::max());
        *static_cast<len_t*>(_buffer) = count;
    }

    size_t size(T range) { return std::distance(std::begin(range), std::end(range)) + sizeof(len_t); }
    size_t alignment() { return sizeof(len_t); }
};

template <>
struct object_serializer<const char*> {
    static constexpr size_t max_len = 50;
    [[gnu::always_inline]]
    void serialize(const char* val, void* _buffer) {

        if (!val) {
            val = "<null>";
        }
        auto buffer = static_cast<unsigned char*>(_buffer);
        size_t len = 0;
        unsigned char tmp;
        // destination is in "pascal string" layout
        while (len < max_len -1 && (tmp = try_load(val + len, '?')) != 0) {
            buffer[len++ + 1] = tmp;
        }
        *buffer = len;
    }

    template<typename T>
    [[gnu::always_inline]]
    T try_load(const T* bad_addr, T alt) {
        T ret;
        if (!safe_load(bad_addr, ret)) {
            return alt;
        }
        return ret;
    }

    size_t size(const char* arg) { return max_len; }
    size_t alignment() { return 1; }
};

template <size_t idx, size_t N, typename... args>
struct serializer {
    [[gnu::always_inline]] // otherwise ld can discard a duplicate function (due to safe_load())
    static void write(void* buffer, size_t offset, std::tuple<args...> as) {
        auto arg = std::get<idx>(as);
        object_serializer<decltype(arg)> s;
        offset = align_up(offset, s.alignment());
        s.serialize(arg, (u8*)buffer + offset);
        return serializer<idx + 1, N, args...>::write(buffer, offset + s.size(arg), as);
    }
    static size_t size(size_t offset, const std::tuple<args...>& as) {
        auto arg = std::get<idx>(as);
        typedef typename std::tuple_element<idx, std::tuple<args...>>::type argtype;
        object_serializer<argtype> s;
        offset = align_up(offset, s.alignment());
        return serializer<idx + 1, N, args...>::size(offset + s.size(arg), as);
    }
};

template <size_t N, typename... args>
struct serializer<N, N, args...> {
    static void write(void* buffer, size_t offset, std::tuple<args...> as) {
    }
    static size_t size(size_t offset, const std::tuple<args...>& as) {
        return offset;
    }
};

typedef std::tuple<const std::type_info*, unsigned long> tracepoint_id;

class tracepoint_base {
public:
    struct probe {
        virtual ~probe() {}
        virtual void hit() = 0;
    };
public:
    explicit tracepoint_base(unsigned _id, const std::type_info& _tp_type,
                             const char* _name, const char* _format);
    ~tracepoint_base();

    void add_probe(probe* p);
    void del_probe(probe* p);

    bool enabled() const {
        return _logging;
    }
    bool backtrace() const {
        return _backtrace;
    }

    void enable(bool = true);
    void backtrace(bool);
    
    const tracepoint_id id;
    const char* name;
    const char* format;
    const char* sig;
    typedef boost::intrusive::list_member_hook<> tp_list_link_type;
    tp_list_link_type tp_list_link;
    static boost::intrusive::list<
        tracepoint_base,
        boost::intrusive::member_hook<tracepoint_base,
                                      tp_list_link_type,
                                      &tracepoint_base::tp_list_link>,
        boost::intrusive::constant_time_size<false>
        > tp_list;
    static const size_t backtrace_len = 10;
protected:
    bool _backtrace = false;
    bool _logging = false;
    bool active = false; // logging || !probes.empty()
    osv::rcu_ptr<std::vector<probe*>> probes_ptr;
    mutex probes_mutex;
    void run_probes();
    void log_backtrace(trace_record* tr, u8*& buffer) {
        if (!tr->backtrace) {
            return;
        }
        do_log_backtrace(tr, buffer);
    }
    void do_log_backtrace(trace_record* tr, u8*& buffer);
    trace_record* allocate_trace_record(size_t size);
private:
    void try_enable();
    void activate();
    void deactivate();
    void activate(const tracepoint_id &, void * site, void * slow_path);
    void deactivate(const tracepoint_id &, void * site, void * slow_path);
    void update();
    static std::unordered_set<tracepoint_id>& known_ids();
};

namespace {

template <unsigned id, typename assign_fn, assign_fn* assign>
class tracepointv;

template <unsigned _id,
          typename... s_args,
          typename... r_args,
          std::tuple<s_args...>(*assign)(r_args...)>
class tracepointv<_id, std::tuple<s_args...>(r_args...), assign>
    : public tracepoint_base
{
public:
    explicit tracepointv(const char* name, const char* format)
        : tracepoint_base(_id, typeid(*this), name, format) {
        sig = signature();
    }

    inline void operator()(r_args... as);

    void trace_slow_path(std::tuple<s_args...> as) __attribute__((cold)) {
        if (active) {
            arch::irq_flag_notrace irq;
            irq.save();
#if CONF_lazy_stack
            if (sched::preemptable() && irq.enabled()) {
                arch::ensure_next_stack_page();
            }
#endif
            arch::irq_disable_notrace();
            log(as);
            run_probes();
            irq.restore();
        }
    }
    void log(const std::tuple<s_args...>& as) {
        if (!_logging) {
            return;
        }
        auto tr = allocate_trace_record(payload_size(as));
        auto buffer = tr->buffer;
        log_backtrace(tr, buffer);
        serialize(buffer, as);
        barrier();
        tr->tp = this; // do this last to indicate the record is complete
        if (_trace_log) {
            _trace_log->write(tr);
        }
    }
    void serialize(void* buffer, std::tuple<s_args...> as) {
        serializer<0, sizeof...(s_args), s_args...>::write(buffer, 0, as);
    }
    size_t payload_size(const std::tuple<s_args...>& as) const {
        return serializer<0, sizeof...(s_args), s_args...>::size(0, as);
    }
    // Python struct style signature H=u16, I=u32, Q=u64 etc
    // Parsed by SlidingUnpacker from scripts/osv/trace.py
    const char* signature() const {
        return signature_helper<s_args...>::sig;
    }
};

}

template <typename... args>
inline
std::tuple<args...> identity_assign(args... as)
{
    return std::make_tuple(as...);
}

template <unsigned id, typename... args>
using tracepoint = tracepointv<id,
                               decltype(identity_assign<args...>),
                               identity_assign<args...>>;

//#define tracepoint(...) tracepoint<__COUNTER__, ##__VA_ARGS__>

static inline const char *trace_strip_prefix(const char *name)
{
    return (strncmp(name, "trace_", 6) == 0) ? (name+6) : name;
}
#define TRACEPOINT(name, fmt, ...) \
    tracepoint<__COUNTER__, ##__VA_ARGS__> name(trace_strip_prefix(#name), fmt);
#define TRACEPOINTV(name, fmt, assign) \
    tracepointv<__COUNTER__, decltype(assign), assign> name(trace_strip_prefix(#name), fmt);


#include <arch-trace.hh>

#endif /* TRACE_HH_ */
