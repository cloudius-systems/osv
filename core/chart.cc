#include "arch.hh"
#include <osv/debug.hh>
#include <osv/sched.hh>
#include "drivers/clock.hh"
#include <osv/barrier.hh>
#include <osv/boot.hh>

double boot_time_chart::to_msec(u64 time)
{
    return (double)clock::get()->processor_to_nano(time) / 1000000;
}

void boot_time_chart::print_one_time(int index)
{
    auto field = arrays[index].stamp;
    auto last = arrays[index - 1].stamp;
    auto initial = arrays[0].stamp;
    printf("\t%s: %.2fms, (+%.2fms)\n", arrays[index].str, to_msec(field - initial), to_msec(field - last));
}

void boot_time_chart::event(const char *str)
{
    event(_event++, str, processor::ticks());
}

void boot_time_chart::event(int event_idx, const char *str)
{
    event(event_idx, str, processor::ticks());
}

void boot_time_chart::event(int event_idx, const char *str, u64 stamp)
{
    arrays[event_idx].str = str;
    arrays[event_idx].stamp = stamp;
}

void boot_time_chart::print_chart()
{
    if (clock::get()->processor_to_nano(10000) == 0) {
        debug("Skipping bootchart: please run this with a clocksource that can do ticks/nanoseconds conversion.\n");
        return;
    }
    int events = _event;
    for (auto i = 1; i < events; ++i) {
        print_one_time(i);
    }
}

void boot_time_chart::print_total_time()
{
    auto last = arrays[_event - 1].stamp;
    auto initial = arrays[0].stamp;
    printf("Booted up in %.2f ms\n", to_msec(last - initial));
}
