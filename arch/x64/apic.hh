#ifndef APIC_HH_
#define APIC_HH_

namespace processor {

class apic_driver {
public:
    virtual ~apic_driver();
    virtual void self_ipi(unsigned vector) = 0;
    virtual void ipi(unsigned cpu, unsigned vector) = 0;
    virtual void eoi() = 0;
};

extern apic_driver* apic;

}

#endif /* APIC_HH_ */
