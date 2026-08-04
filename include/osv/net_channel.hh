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
#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>
#include <lockfree/ring.hh>
#include <functional>
#include <unordered_map>
#include <vector>
#include <osv/rcu.hh>
#include <osv/rcu-hashtable.hh>
#include <bsd/porting/netport.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>
#include <osv/file.h>

struct mbuf;
struct pollreq;

// The BSD headers #define a macro called free, so including mempool
// directly will yield trouble. We only need those two functions.
extern void memory::free_page(void* v);
extern void* memory::alloc_page();

// Lock-free queue for moving packets to a single consumer
// Supports waiting via sched::thread::wait_for()
class net_channel {
private:
    std::function<void (mbuf*)> _process_packet;
    ring_spsc<mbuf*, unsigned, 256> _queue;
    sched::thread_handle _waiting_thread CACHELINE_ALIGNED;
    // extra list of threads to wake
    osv::rcu_ptr<std::vector<pollreq*>> _pollers;
    osv::rcu_hashtable<epoll_ptr> _epollers;
    mutex _pollers_mutex;
public:
    explicit net_channel(std::function<void (mbuf*)> process_packet)
        : _process_packet(std::move(process_packet)) {}
    // producer: try to push a packet
    bool push(mbuf* m) { return _queue.push(m); }
    // consumer: wake the consumer (best used after multiple push()s)
    void wake() {
#if CONF_lazy_stack_invariant
        assert(!sched::thread::current()->is_app());
#endif
        _waiting_thread.wake_from_kernel_or_with_irq_disabled();
        if (_pollers || !_epollers.empty()) {
            wake_pollers();
        }
    }
    // consumer: consume all available packets using process_packet()
    void process_queue();
    // add/remove current thread from poller list
    void add_poller(pollreq& pr);
    void del_poller(pollreq& pr);
    void add_epoll(const epoll_ptr& ep);
    void del_epoll(const epoll_ptr& ep);
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
    // Batched producer path: classify+push like post_packet(), but instead of
    // waking the channel per packet, record it in "batch" (deduplicated).  The
    // caller wakes every distinct channel once via batch.flush() at the end of
    // a receive-drain pass.  This turns a NIC receiver's one-wake-per-packet
    // into one-wake-per-channel-per-pass, and because the flushed wakes run
    // back-to-back without any intervening reschedule, thread::wake_impl()'s
    // per-target-CPU IPI coalescing (incoming_wakeups_mask) collapses them into
    // at most one wakeup IPI per destination CPU per pass instead of per packet.
    // MUST be called (and batch.flush()ed) under a single osv::rcu_read_lock so
    // the recorded net_channel pointers (which are rcu_dispose()d on connection
    // teardown) stay valid until flushed.
    bool post_packet(mbuf* m, class net_channel_wake_batch& batch);
private:
    net_channel* classify_ipv4_tcp(mbuf* m);
private:
    struct item {
        item(const ipv4_tcp_conn_id& key, net_channel* chan) : key(key), chan(chan) {}
        ipv4_tcp_conn_id key;
        net_channel* chan;
    };
    struct item_hash : private std::hash<ipv4_tcp_conn_id> {
        size_t operator()(const item& i) const { return std::hash<ipv4_tcp_conn_id>::operator()(i.key); }
    };
    struct key_item_compare {
        bool operator()(const ipv4_tcp_conn_id& key, const item& item) const {
            return key == item.key;
        }
    };
    using ipv4_tcp_channels = osv::rcu_hashtable<item, item_hash>;
    mutex _mtx;
    ipv4_tcp_channels _ipv4_tcp_channels;
};

// Collects the distinct net_channels touched during one NIC receive-drain pass
// so their wakes can be issued together at the end of the pass (see
// classifier::post_packet(m, batch)).  Deduplicates so a channel that received
// several packets in the pass is woken only once.  A small inline capacity
// avoids heap traffic on the hot RX path; overflow spills to a heap vector.
// Must be used and flush()ed under the same osv::rcu_read_lock that guarded the
// post_packet() calls (net_channel is rcu_dispose()d on teardown).
class net_channel_wake_batch {
public:
    // record a channel (deduplicated)
    void add(net_channel* nc) {
        for (unsigned i = 0; i < _n; i++) {
            if (_inline[i] == nc) {
                return;
            }
        }
        if (_spill) {
            for (auto* c : *_spill) {
                if (c == nc) {
                    return;
                }
            }
        }
        if (_n < inline_capacity) {
            _inline[_n++] = nc;
        } else {
            if (!_spill) {
                _spill = new std::vector<net_channel*>;
            }
            _spill->push_back(nc);
        }
    }
    // wake every distinct recorded channel once, then reset
    void flush() {
        for (unsigned i = 0; i < _n; i++) {
            _inline[i]->wake();
        }
        _n = 0;
        if (_spill) {
            for (auto* c : *_spill) {
                c->wake();
            }
            delete _spill;
            _spill = nullptr;
        }
    }
    bool empty() const { return _n == 0 && !_spill; }
    ~net_channel_wake_batch() { delete _spill; }
private:
    static const unsigned inline_capacity = 16;
    unsigned _n = 0;
    net_channel* _inline[inline_capacity];
    std::vector<net_channel*>* _spill = nullptr;
};

#endif /* NETCHANNEL_HH_ */
