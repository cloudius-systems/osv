/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/net_channel.hh>
#include <osv/poll.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/tcp.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/netisr.h>

#include <osv/net_trace.hh>
#include <osv/percpu.hh>
#include <lockfree/ring.hh>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>
#include <osv/kernel_config_core_epoll.h>
#include <osv/fork_arena.hh>

#if CONF_fork
// The poller/epoll/classifier structures below are populated by APPLICATION
// threads (a PG backend adding its connection socket to epoll) but WALKED by
// the virtio-net RX path (net_channel::wake_pollers, classifier::post_packet)
// running in a kernel thread / IRQ context in AS0 with preemption disabled.
// If their RCU vectors / hashtable nodes lived in the COW fork arena, those
// arena VAs are not mapped in the RX thread's address space -> the RX walk
// takes a page fault while !preemptable() -> "Assertion failed:
// sched::preemptable() (page_fault)".  Force every such allocation onto the
// shared identity kernel heap so the RX path sees the same nodes in every AS,
// the same rule already applied to struct file, ramfs nodes and the signal
// waiters list (see fork_arena.hh).
#define NET_SHARED_KH fork_arena::kernel_heap_scope _net_kh
#else
#define NET_SHARED_KH do {} while (0)
#endif

void net_channel::process_queue()
{
    mbuf* m;
    while (_queue.pop(m)) {
        _process_packet(m);
    }
}

void net_channel::wake_pollers()
{
#if CONF_lazy_stack_invariant
    assert(!sched::thread::current()->is_app());
#endif
    WITH_LOCK(osv::rcu_read_lock) {
        auto pl = _pollers.read();
        if (pl) {
            for (pollreq* pr : *pl) {
                // net_channel is self synchronizing
                pr->_awake.store(true, std::memory_order_relaxed);
                pr->_poll_thread.wake_from_kernel_or_with_irq_disabled();
            }
        }
#if CONF_core_epoll
        // can't call epoll_wake from rcu, so copy the data
        if (!_epollers.empty()) {
            _epollers.reader_for_each([&] (const epoll_ptr& ep) {
                epoll_wake_in_rcu(ep);
            });
        }
#endif
    }
}

void net_channel::add_poller(pollreq& pr)
{
    WITH_LOCK(_pollers_mutex) {
        NET_SHARED_KH;
        auto old = _pollers.read_by_owner();
        std::unique_ptr<std::vector<pollreq*>> neww{new std::vector<pollreq*>};
        if (old) {
            *neww = *old;
        }
        neww->push_back(&pr);
        _pollers.assign(neww.release());
        osv::rcu_dispose(old);
    }
}

void net_channel::del_poller(pollreq& pr)
{
    WITH_LOCK(_pollers_mutex) {
        NET_SHARED_KH;
        auto old = _pollers.read_by_owner();
        std::unique_ptr<std::vector<pollreq*>> neww{new std::vector<pollreq*>};
        if (old) {
            *neww = *old;
        }
        neww->erase(std::remove(neww->begin(), neww->end(), &pr), neww->end());
        _pollers.assign(neww.release());
        osv::rcu_dispose(old);
    }
}

void net_channel::add_epoll(const epoll_ptr& ep)
{
#if CONF_core_epoll
    WITH_LOCK(_pollers_mutex) {
        NET_SHARED_KH;
        if (!_epollers.owner_find(ep)) {
            _epollers.insert(ep);
        }
    }
#endif
}

void net_channel::del_epoll(const epoll_ptr& ep)
{
#if CONF_core_epoll
    WITH_LOCK(_pollers_mutex) {
        NET_SHARED_KH;
        auto i = _epollers.owner_find(ep);
        if (i) {
            _epollers.erase(i);
        }
    }
#endif
}

classifier::classifier()
{
}

void classifier::add(ipv4_tcp_conn_id id, net_channel* channel)
{
    WITH_LOCK(_mtx) {
        NET_SHARED_KH;
        _ipv4_tcp_channels.emplace(id, channel);
    }
}

void classifier::remove(ipv4_tcp_conn_id id)
{
    WITH_LOCK(_mtx) {
        auto i = _ipv4_tcp_channels.owner_find(id,
                std::hash<ipv4_tcp_conn_id>(), key_item_compare());
        assert(i);
        _ipv4_tcp_channels.erase(i);
    }
}

bool classifier::post_packet(mbuf* m)
{
    if (auto nc = classify_and_push(m)) {
        // FIXME: find a way to batch wakes
        nc->wake();
        return true;
    }
    return false;
}

net_channel* classifier::classify_and_push(mbuf* m)
{
#if CONF_lazy_stack_invariant
    assert(!sched::thread::current()->is_app());
#endif
    WITH_LOCK(osv::rcu_read_lock) {
        if (auto nc = classify_ipv4_tcp(m)) {
            log_packet_in(m, NETISR_ETHER);
            if (!nc->push(m)) {
                return nullptr;
            }
            return nc;
        }
    }
    return nullptr;
}

// must be called with rcu lock held
net_channel* classifier::classify_ipv4_tcp(mbuf* m)
{
    caddr_t h = m->m_hdr.mh_data;
    if (unsigned(m->m_hdr.mh_len) < ETHER_HDR_LEN + sizeof(ip)) {
        return nullptr;
    }
    auto ether_hdr = reinterpret_cast<ether_header*>(h);
    if (ntohs(ether_hdr->ether_type) != ETHERTYPE_IP) {
        return nullptr;
    }
    h += ETHER_HDR_LEN;
    auto ip_hdr = reinterpret_cast<ip*>(h);
    unsigned ip_size = ip_hdr->ip_hl << 2;
    if (ip_size < sizeof(ip)) {
        return nullptr;
    }
    if (ip_hdr->ip_p != IPPROTO_TCP) {
        return nullptr;
    }
    if (ntohs(ip_hdr->ip_off) & ~IP_DF) {
        return nullptr;
    }
    auto src_addr = ip_hdr->ip_src;
    auto dst_addr = ip_hdr->ip_dst;
    h += ip_size;
    auto tcp_hdr = reinterpret_cast<tcphdr*>(h);
    if (tcp_hdr->th_flags & (TH_SYN | TH_FIN | TH_RST)) {
	    return nullptr;
    }
    auto src_port = ntohs(tcp_hdr->th_sport);
    auto dst_port = ntohs(tcp_hdr->th_dport);
    auto id = ipv4_tcp_conn_id{src_addr, dst_addr, src_port, dst_port};
    auto i = _ipv4_tcp_channels.reader_find(id,
            std::hash<ipv4_tcp_conn_id>(), key_item_compare());
    if (!i) {
        return nullptr;
    }
    return i->chan;
}

// ---------------------------------------------------------------------------
// RX wake fan-out: per-CPU wake-worker threads (net_wake_dispatch).
//
// Each CPU runs one wake-worker thread with a single-producer/single-consumer
// ring of net_channel*.  The RX receiver is the sole producer into every
// ring, and each worker is the sole consumer of its own ring, so the rings
// stay SPSC.  defer_wake() round-robins channels across the workers (skipping
// the receiver's own CPU so the receiver keeps draining); flush() wakes the
// workers that were fed this pass; each worker calls net_channel::wake() from
// its own CPU, spreading the cross-CPU IPIs and the woken-backend scheduling
// across the idle cores instead of serializing on the receiver CPU.
// ---------------------------------------------------------------------------
namespace net_wake_dispatch {

// Bounded per-CPU handoff ring.  512 entries is well above the RX burst of
// distinct connections a single drain pass produces; on overflow the caller
// wakes inline, so a full ring only costs a little serialization, never loss.
static const unsigned RING_SIZE = 512;

struct worker {
    ring_spsc<net_channel*, unsigned, RING_SIZE> ring;
    sched::thread* thread = nullptr;
    std::atomic<bool> fed{false};
};

static std::vector<worker*> workers;   // index == cpu id
static bool _enabled = false;
static std::atomic<unsigned> _rr{0};   // round-robin cursor

bool enabled()
{
    return _enabled;
}

static void worker_loop(worker* w)
{
    while (true) {
        sched::thread::wait_until([w] {
            return !w->ring.empty();
        });
        w->fed.store(false, std::memory_order_relaxed);
        net_channel* nc;
        while (w->ring.pop(nc)) {
            nc->wake();
        }
    }
}

void init()
{
    if (const char* e = getenv("OSV_NET_DISPATCH_FANOUT")) {
        if (e[0] != '0') {
            _enabled = true;
        }
    }
    if (!_enabled) {
        return;
    }
    unsigned n = sched::cpus.size();
    workers.resize(n, nullptr);
    for (unsigned i = 0; i < n; i++) {
        auto w = new worker();
        w->thread = sched::thread::make([w] { worker_loop(w); },
                sched::thread::attr().pin(sched::cpus[i])
                    .name(std::string("net-wake") + std::to_string(i)));
        workers[i] = w;
        w->thread->start();
    }
    printf("net_wake_dispatch ARMED (%u per-CPU wake workers)\n", n);
}

bool defer_wake(net_channel* nc)
{
    if (!_enabled) {
        return false;
    }
    unsigned n = workers.size();
    if (n < 2) {
        return false;   // nothing to fan out to
    }
    unsigned self = sched::cpu::current()->id;
    // Pick the next CPU round-robin, skipping the receiver's own CPU so it
    // keeps draining the RX ring while the wakes run elsewhere.
    unsigned idx = _rr.fetch_add(1, std::memory_order_relaxed) % n;
    if (idx == self) {
        idx = (idx + 1) % n;
    }
    worker* w = workers[idx];
    if (!w || !w->ring.push(nc)) {
        return false;   // ring full -> caller wakes inline
    }
    w->fed.store(true, std::memory_order_relaxed);
    return true;
}

void flush()
{
    if (!_enabled) {
        return;
    }
    for (worker* w : workers) {
        if (w && w->fed.load(std::memory_order_relaxed)) {
            w->fed.store(false, std::memory_order_relaxed);
            w->thread->wake();
        }
    }
}

}
