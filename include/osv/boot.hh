#ifndef BOOT_HH
#define BOOT_HH

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
private:
    int _event = 0;

    void print_one_time(int index);
    double to_msec(u64 time);
};
#endif
