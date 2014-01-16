/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef NETCHANNEL_HH_
#define NETCHANNEL_HH_

#include <osv/mutex.h>
#include <osv/sched.hh>
#include <lockfree/ring.hh>
#include <functional>

struct mbuf;

// Lock-free queue for moving packets to a single consumer
// Supports waiting via sched::thread::wait_for()
class net_channel {
private:
    std::function<void (mbuf*)> _process_packet;
    ring_spsc<mbuf*, 256> _queue;
    sched::thread_handle _waiting_thread CACHELINE_ALIGNED;
public:
    explicit net_channel(std::function<void (mbuf*)> process_packet)
        : _process_packet(std::move(process_packet)) {}
    // producer: try to push a packet
    bool push(mbuf* m) { return _queue.push(m); }
    // consumer: wake the consumer (best used after multiple push()s)
    void wake() { _waiting_thread.wake(); }
    // consumer: consume all available packets using process_packet()
    void process_queue();
private:
    friend class sched::wait_object<net_channel>;
};

namespace sched {

template <>
class wait_object<net_channel> {
private:
    net_channel& _nc;
public:
    explicit wait_object(net_channel& nc, mutex* mtx = nullptr) : _nc(nc) {}
    bool poll() { return _nc._queue.size(); }
    void arm() { _nc._waiting_thread.reset(*sched::thread::current()); }
    void disarm() { _nc._waiting_thread.clear(); }
};

}

#endif /* NETCHANNEL_HH_ */
