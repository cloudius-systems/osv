/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "eventlist.hh"

eventman* eventman::_instance = nullptr;

handlerslist::handlerslist()
{
    for (int i=0; i<max_handlers; i++) {
        _handlers[i] = nullptr;
    }
}

handlerslist::~handlerslist()
{
    for (int i=0; i<max_handlers; i++) {
        _handlers[i] = nullptr;
    }
}

int handlerslist::find_free(void)
{
    for (int i=0; i<max_handlers; i++) {
        if (_handlers[i] == nullptr) {
            return (i);
        }
    }

    return (-1);
}

int handlerslist::add(eventhandler _handler)
{
    int idx = find_free();
    if (idx != -1) {
        _handlers[idx] = _handler;
    }

    return (idx);
}

bool handlerslist::remove(int idx)
{
    if (idx >= max_handlers) {
        return (false);
    }

    if (_handlers[idx] == nullptr) {
        return (false);
    }

    _handlers[idx] = nullptr;
    return (true);
}

int handlerslist::clone_to(eventhandler* hlist, int max)
{
    int num_slots = std::min(max, max_handlers);
    for (int i=0; i<num_slots; i++) {
        hlist[i] = _handlers[i];
    }

    return (num_slots);
}

eventman::eventman()
{

}

eventman::~eventman()
{
    // FIXME: what about the mutex?...
    for (auto it=_events.begin(); it != _events.end(); it++) {
        delete (it->second);
    }
}

handlerslist* eventman::get_list(const char* ev_name)
{
    _lock.lock();
    auto it = _events.find(ev_name);
    if (it == _events.end()) {
        _lock.unlock();
        return (nullptr);
    }

    _lock.unlock();
    return (it->second);
}

// Creates a new event
bool eventman::create_event(const char *ev_name)
{
    _lock.lock();
    // Already created
    if (get_list(ev_name) != nullptr) {
        _lock.unlock();
        return (false);
    }

    handlerslist* lst = new handlerslist();
    _events.insert(std::make_pair(ev_name, lst));

    _lock.unlock();
    return (true);
}

// Registers a callback within a chain
int eventman::register_event(const char *ev_name, std::function<void()> _handler)
{
    _lock.lock();
    handlerslist* hlist = get_list(ev_name);
    if (hlist == nullptr) {
        _lock.unlock();
        return (-1);
    }

    int idx = hlist->add(_handler);

    _lock.unlock();
    return (idx);
}


bool eventman::deregister_event(const char *ev_name, int idx)
{
    _lock.lock();
    handlerslist* hlist = get_list(ev_name);
    if (hlist == nullptr) {
        _lock.unlock();
        return (false);
    }

    bool rc = hlist->remove(idx);

    _lock.unlock();
    return (rc);
}

bool eventman::invoke_event(const char *ev_name)
{
    _lock.lock();
    handlerslist* hlist = get_list(ev_name);
    if (hlist == nullptr) {
        _lock.unlock();
        return (false);
    }

    // Clone & unlock
    eventhandler handlers[max_handlers];
    int num_slots = hlist->clone_to(handlers, max_handlers);
    _lock.unlock();

    // Invoke handlers
    for (int i=0; i<num_slots; i++) {
        if (handlers[i] != nullptr) {
            handlers[i]();
        }
    }

    return (true);
}


