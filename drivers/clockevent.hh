#ifndef CLOCKEVENT_HH_
#define CLOCKEVENT_HH_

#include "types.hh"

class clock_event_callback {
public:
    virtual ~clock_event_callback();
    virtual void fired() = 0;
};

class clock_event_driver {
public:
    virtual ~clock_event_driver();
    virtual void set(u64 time) = 0;
    void set_callback(clock_event_callback* callback);
    clock_event_callback* callback() const;
protected:
    clock_event_callback* _callback;

};
extern clock_event_driver* clock_event;


#endif /* CLOCKEVENT_HH_ */
