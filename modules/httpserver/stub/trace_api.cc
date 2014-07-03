#include <osv/tracecontrol.hh>

std::vector<trace::event_info>
trace::get_event_info()
{
    return std::vector<event_info>();
}

std::vector<trace::event_info>
trace::get_event_info(const std::regex & ex)
{
    return std::vector<event_info>();
}

std::vector<trace::event_info>
trace::set_event_state(const std::regex & ex, bool enable, bool backtrace)
{
    return std::vector<event_info>();
}

trace::event_info
trace::get_event_info(const ext_id & id)
{
    throw std::invalid_argument(id);
}

trace::event_info
trace::set_event_state(const ext_id & id, bool enable, bool backtrace)
{
    throw std::invalid_argument(id);
}

std::string
trace::create_trace_dump()
{
    throw std::invalid_argument("this is just a dummy stub");
}

