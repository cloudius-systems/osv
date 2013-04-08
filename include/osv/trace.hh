#ifndef TRACE_HH_
#define TRACE_HH_

#include <iostream>
#include <tuple>
#include <boost/format.hpp>

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

template <typename... s_args,
          typename... r_args,
          typename assigner_type<storage_args<s_args...>, runtime_args<r_args...>>::type assign>
class tracepointv<storage_args<s_args...>, runtime_args<r_args...>, assign>
{
public:
    explicit tracepointv(const char* _name,
                 const char* _format)
        : name(_name), format(_format) {}
    void operator()(r_args... as) {
        trace_slow_path(assign(as...));
    }
    void trace_slow_path(std::tuple<s_args...> as) __attribute__((cold)) {
        if (enabled) {
            std::cout << name << " " << format_tuple(format, as) << "\n";
        }
    }
    const char* name;
    const char* format;
    bool enabled = true;
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
