#ifndef EVENTLIST_H
#define EVENTLIST_H

#include <map>
#include <string>
#include <functional>

#include "mutex.hh"

typedef std::function<void ()> eventhandler;
const int max_handlers = 32;

#define event_manager (eventman::instance())

//
// Manage a list of handlers
// All functions in this class assumed to be called with a lock (not-sync'd)
//
class handlerslist {
public:

    handlerslist();
    virtual ~handlerslist();

    int add(eventhandler _handler);
    bool remove(int idx);
    int clone_to(eventhandler* hlist, int max);

private:
    int find_free(void);
    eventhandler _handlers[max_handlers];
};

//
// Manages events, each with an associated list of callbacks
// that are dispatched when the event is signaled
//
class eventman {
public:
    eventman();
    virtual ~eventman();

    static eventman* instance() {
        if (_instance == nullptr) {
            _instance = new eventman();
        }
        return (_instance);
    }

    // Creates a new event
    bool create_event(const char *ev_name);

    // Registers a callback within a chain
    int register_event(const char *ev_name, std::function<void()> _handler);
    bool deregister_event(const char *ev_name, int idx);
    bool invoke_event(const char *ev_name);

private:

    static eventman* _instance;
    handlerslist* get_list(const char* ev_name);

    // Lock
    mutex _lock;
    // Map to list of events
    std::map<std::string, handlerslist*> _events;
};

#endif
