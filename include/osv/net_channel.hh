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
#include <unordered_map>
#include <osv/rcu.hh>
#include <bsd/porting/netport.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>

struct mbuf;
struct pollreq;

// Lock-free queue for moving packets to a single consumer
// Supports waiting via sched::thread::wait_for()
class net_channel {
private:
    std::function<void (mbuf*)> _process_packet;
    ring_spsc<mbuf*, 256> _queue;
    sched::thread_handle _waiting_thread CACHELINE_ALIGNED;
    // extra list of threads to wake
    osv::rcu_ptr<std::vector<pollreq*>> _pollers;
    mutex _pollers_mutex;
public:
    explicit net_channel(std::function<void (mbuf*)> process_packet)
        : _process_packet(std::move(process_packet)) {}
    // producer: try to push a packet
    bool push(mbuf* m) { return _queue.push(m); }
    // consumer: wake the consumer (best used after multiple push()s)
    void wake() {
        _waiting_thread.wake();
        if (_pollers) {
            wake_pollers();
        }
    }
    // consumer: consume all available packets using process_packet()
    void process_queue();
    // add/remove current thread from poller list
    void add_poller(pollreq& pr);
    void del_poller(pollreq& pr);
private:
    void wake_pollers();
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

struct ipv4_tcp_conn_id {
    ipv4_tcp_conn_id(in_addr src_addr, in_addr dst_addr, in_port_t src_port, in_port_t dst_port)
        : src_addr(src_addr), dst_addr(dst_addr), src_port(src_port), dst_port(dst_port) {}

    in_addr src_addr;
    in_addr dst_addr;
    in_port_t src_port;
    in_port_t dst_port;

    size_t hash() const {
        // FIXME: protection against hash attacks?
        return src_addr.s_addr ^ dst_addr.s_addr ^ src_port ^ dst_port;
    }
    bool operator==(const ipv4_tcp_conn_id& x) const {
        return src_addr == x.src_addr
            && dst_addr == x.dst_addr
            && src_port == x.src_port
            && dst_port == x.dst_port;
    }
};

namespace std {

template <>
struct hash<ipv4_tcp_conn_id> {
    size_t operator()(ipv4_tcp_conn_id x) const { return x.hash(); }
};

}

class classifier {
public:
    classifier();
    // consumer side operations
    void add(ipv4_tcp_conn_id id, net_channel* channel);
    void remove(ipv4_tcp_conn_id id);
    // producer side operations
    bool post_packet(mbuf* m);
private:
    net_channel* classify_ipv4_tcp(mbuf* m);
private:
    using ipv4_tcp_channels = std::unordered_map<ipv4_tcp_conn_id, net_channel*>;
    mutex _mtx;
    // FIXME: use a fine-grained rcu hash table
    osv::rcu_ptr<ipv4_tcp_channels, osv::rcu_deleter<ipv4_tcp_channels>> _ipv4_tcp_channels;
};

#endif /* NETCHANNEL_HH_ */
