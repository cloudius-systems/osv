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
#include <osv/rcu-hashtable.hh>
#include <bsd/porting/netport.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>
#include <osv/file.h>

#ifdef INET6
#include <bsd/sys/netinet/ip6.h>
#endif /* INET6 */

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
    ring_spsc<mbuf*, 256> _queue;
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
        _waiting_thread.wake();
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

#ifdef INET6

struct ipv6_tcp_conn_id {
    in6_addr src_addr;
    in6_addr dst_addr;
    in_port_t src_port;
    in_port_t dst_port;

    static uint32_t hash_in6_addr(const in6_addr &addr) {
        uint32_t *a = (uint32_t*) &addr.s6_addr;
        return ( a[0] ^ a[1] ^ a[2] ^ a[3] );
    }
    size_t hash() const {
        // FIXME: protection against hash attacks?
        return hash_in6_addr(src_addr) ^ hash_in6_addr(dst_addr) ^ src_port ^ dst_port;
    }
    bool operator==(const ipv6_tcp_conn_id& x) const {
        return memcmp(&src_addr, &x.src_addr, sizeof(src_addr)) == 0
            && memcmp(&dst_addr, &x.dst_addr, sizeof(dst_addr)) == 0
            && src_port == x.src_port
            && dst_port == x.dst_port;
    }
};

#endif /* INET6 */

namespace std {

template <>
struct hash<ipv4_tcp_conn_id> {
    size_t operator()(ipv4_tcp_conn_id x) const { return x.hash(); }
};

#ifdef INET6

template <>
struct hash<ipv6_tcp_conn_id> {
    size_t operator()(ipv6_tcp_conn_id x) const { return x.hash(); }
};

#endif /* INET6 */

}

class classifier {
public:
    classifier();
    // consumer side operations
    void add(const ipv4_tcp_conn_id& id, net_channel* channel);
    void remove(const ipv4_tcp_conn_id& id);
#ifdef INET6
    void add(const ipv6_tcp_conn_id& id, net_channel* channel);
    void remove(const ipv6_tcp_conn_id& id);
#endif /* INET6 */

    // producer side operations
    bool post_packet(mbuf* m);
private:
    net_channel* classify_packet(mbuf* m);
    net_channel* classify_ipv4_tcp(mbuf* m, struct ip* ip, size_t ip_len);
#ifdef INET6
    net_channel* classify_ipv6_tcp(mbuf* m, struct ip6_hdr* ip, size_t ip_len);
#endif /* INET6 */

private:
    template <class KeyType>
    struct item {
        item(const KeyType& key, net_channel* chan) : key(key), chan(chan) {}
        KeyType key;
        net_channel* chan;
    };
    template <class KeyType>
    struct item_hash : private std::hash<KeyType> {
        size_t operator()(const item<KeyType>& i) const { return std::hash<KeyType>::operator()(i.key); }
    };
    template <class KeyType>
    struct key_item_compare {
        bool operator()(const KeyType& key, const item<KeyType>& item) const {
            return key == item.key;
        }
    };
    mutex _mtx;
    using ipv4_tcp_channels = osv::rcu_hashtable<item<ipv4_tcp_conn_id>, item_hash<ipv4_tcp_conn_id>>;
    ipv4_tcp_channels _ipv4_tcp_channels;
#ifdef INET6
    using ipv6_tcp_channels = osv::rcu_hashtable<item<ipv6_tcp_conn_id>, item_hash<ipv6_tcp_conn_id>>;
    ipv6_tcp_channels _ipv6_tcp_channels;
#endif /* INET6 */
};

#endif /* NETCHANNEL_HH_ */
