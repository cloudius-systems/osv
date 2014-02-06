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
    arrays[_event].str  = str;
    arrays[_event++].stamp = processor::ticks();
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
