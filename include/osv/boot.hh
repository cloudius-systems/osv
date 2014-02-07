#ifndef BOOT_HH
#define BOOT_HH

#include "arch-setup.hh"

class time_element {
public:
    const char *str;
    u64 stamp;
};

class boot_time_chart {
public:
    void event(const char *str);
    void print_chart();
    time_element arrays[16];
    friend void arch_setup_free_memory();
private:
    // Can we keep it at 0 and let the initial two users increment it?  No, we
    // cannot. The reason is that the code that *parses* those fields run
    // relatively late (the code that takes the measure is so early it cannot
    // call this one directly. Therefore, the measurements would appear in the
    // middle of the list, and we want to preserve order.
    int _event = 2;

    void print_one_time(int index);
    double to_msec(u64 time);
};
#endif
