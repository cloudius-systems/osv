#include "drivers/clockevent.hh"
#include "drivers/clock.hh"
#include "exceptions.hh"
#include "apic.hh"

using namespace processor;

class apic_clock_events : public clock_event_driver {
public:
    explicit apic_clock_events();
    ~apic_clock_events();
    virtual void set(u64 time);
private:
    unsigned _vector;
};

apic_clock_events::apic_clock_events()
    : _vector(idt.register_handler([this] { _callback->fired(); }))
{
    processor::apic->write(apicreg::TMDCR, 0xb); // divide by 1
    processor::apic->write(apicreg::TMICT, 0);
    processor::apic->write(apicreg::LVTT, _vector); // one-shot
}

apic_clock_events::~apic_clock_events()
{
}

void apic_clock_events::set(u64 time)
{
    u64 now = clock::get()->time();
    if (time <= now) {
        _callback->fired();
    } else {
        // FIXME: handle overflow
        apic->write(apicreg::TMICT, time - now);
    }
}

void __attribute__((constructor)) init_apic_clock()
{
    // FIXME: detect
    clock_event = new apic_clock_events;
}
