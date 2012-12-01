#ifndef DRIVERS_CONSOLE_HH
#define DRIVERS_CONSOLE_HH

class Console {
public:
    virtual ~Console() {}
    virtual void write(const char *str) = 0;
    virtual void newline() = 0;
    void writeln(const char *str) { write(str); newline(); }
};

#endif
