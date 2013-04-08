#ifndef TRACE_HH_
#define TRACE_HH_

#include <iostream>
#include <tuple>
#include <boost/format.hpp>
#include <osv/types.h>

void enable_trace();

template <class storage_args, class runtime_args>
class assigner_type;

template <class storage_args, class runtime_args,
          typename assigner_type<storage_args, runtime_args>::type assign>
class tracepointv;

template <typename... args>
struct storage_args
{
};

template <typename... args>
struct runtime_args
{
};

template <typename... s_args, typename... r_args>
struct assigner_type<storage_args<s_args...>, runtime_args<r_args...>> {
    typedef std::tuple<s_args...> (*type)(r_args...);
};

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

template <typename T>
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

template <typename T>
struct signature_char<T*> {
    static const char sig = 'P';
};

template <typename... args>
struct signature_helper;

template <>
struct signature_helper<> {
    static const u64 sig = 0;
};

template <typename arg0, typename... args>
struct signature_helper<arg0, args...> {
    static const u64 sig = signature_char<arg0>::sig
                    | (signature_helper<args...>::sig << 8);
};

class tracepoint_base {
public:
    explicit tracepoint_base(const char* _name, const char* _format)
        : name(_name), format(_format) {}
    const char* name;
    const char* format;
    bool enabled = true;
};

template <typename... s_args,
          typename... r_args,
          typename assigner_type<storage_args<s_args...>, runtime_args<r_args...>>::type assign>
class tracepointv<storage_args<s_args...>, runtime_args<r_args...>, assign>
    : public tracepoint_base
{
public:
    explicit tracepointv(const char* name, const char* format)
        : tracepoint_base(name, format) {}
    void operator()(r_args... as) {
        trace_slow_path(assign(as...));
    }
    void trace_slow_path(std::tuple<s_args...> as) __attribute__((cold)) {
        if (enabled) {
            std::cout << name << " " << format_tuple(format, as) << "\n";
        }
    }
    // Python struct style signature H=u16, I=u32, Q=u64 etc, packed into a
    // u64, lsb=first parameter
    u64 signature() const {
        return signature_helper<s_args...>::sig;
    }
};

template <typename... args>
inline
std::tuple<args...> identity_assign(args... as)
{
    return std::make_tuple(as...);
}

template <typename... args>
using tracepoint = tracepointv<storage_args<args...>,
                               runtime_args<args...>,
                               identity_assign<args...>>;


#endif /* TRACE_HH_ */
