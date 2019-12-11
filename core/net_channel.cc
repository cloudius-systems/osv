/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/net_channel.hh>
#include <osv/poll.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/netinet/ip.h>
#ifdef INET6
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip6.h>
#include <bsd/sys/netinet6/in6.h>
#include <bsd/sys/netinet6/ip6_var.h>
#include <bsd/sys/compat/linux/linux.h>
#include <bsd/sys/compat/linux/linux_socket.h>
#endif /* INET6 */
#include <bsd/sys/netinet/tcp.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/netisr.h>

#include <osv/debug.hh>
#include <osv/net_trace.hh>

std::ostream& operator<<(std::ostream& os, in_addr ia)
{
    auto x = ntohl(ia.s_addr);
    return osv::fprintf(os, "%d.%d.%d.%d",
            (x >> 24) & 255, (x >> 16) & 255, (x >> 8) & 255, x & 255);
}

std::ostream& operator<<(std::ostream& os, const ipv4_tcp_conn_id& id)
{
    return osv::fprintf(os, "{ ipv4 %s:%d -> %s:%d }", id.src_addr, id.src_port, id.dst_addr, id.dst_port);
}

#ifdef INET6

std::ostream& operator<<(std::ostream& os, in6_addr ia)
{
    char ipstr[INET6_ADDRSTRLEN];
    if (inet_ntop(LINUX_AF_INET6, ia.s6_addr, ipstr, sizeof(ipstr)) == 0)
        return os << "????";
    return  os << ipstr;
}

std::ostream& operator<<(std::ostream& os, const ipv6_tcp_conn_id& id)
{
    return osv::fprintf(os, "{ ipv6 %s:%d -> %s:%d }", id.src_addr, id.src_port, id.dst_addr, id.dst_port);
}

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
    WITH_LOCK(osv::rcu_read_lock) {
        auto pl = _pollers.read();
        if (pl) {
            for (pollreq* pr : *pl) {
                // net_channel is self synchronizing
                pr->_awake.store(true, std::memory_order_relaxed);
                pr->_poll_thread.wake();
            }
        }
        // can't call epoll_wake from rcu, so copy the data
        if (!_epollers.empty()) {
            _epollers.reader_for_each([&] (const epoll_ptr& ep) {
                epoll_wake_in_rcu(ep);
            });
        }
    }
}

void net_channel::add_poller(pollreq& pr)
{
    WITH_LOCK(_pollers_mutex) {
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
    WITH_LOCK(_pollers_mutex) {
        if (!_epollers.owner_find(ep)) {
            _epollers.insert(ep);
        }
    }
}

void net_channel::del_epoll(const epoll_ptr& ep)
{
    WITH_LOCK(_pollers_mutex) {
        auto i = _epollers.owner_find(ep);
        if (i) {
            _epollers.erase(i);
        }
    }
}

classifier::classifier()
{
}

void classifier::add(const ipv4_tcp_conn_id& id, net_channel* channel)
{
    WITH_LOCK(_mtx) {
        _ipv4_tcp_channels.emplace(id, channel);
    }
}

void classifier::remove(const ipv4_tcp_conn_id& id)
{
    WITH_LOCK(_mtx) {
        auto i = _ipv4_tcp_channels.owner_find(id,
                std::hash<ipv4_tcp_conn_id>(), key_item_compare<ipv4_tcp_conn_id>());
        assert(i);
        _ipv4_tcp_channels.erase(i);
    }
}

#ifdef INET6

void classifier::add(const ipv6_tcp_conn_id& id, net_channel* channel)
{
    WITH_LOCK(_mtx) {
        _ipv6_tcp_channels.emplace(id, channel);
    }
}

void classifier::remove(const ipv6_tcp_conn_id& id)
{
    WITH_LOCK(_mtx) {
        auto i = _ipv6_tcp_channels.owner_find(id,
                std::hash<ipv6_tcp_conn_id>(), key_item_compare<ipv6_tcp_conn_id>());
        assert(i);
        _ipv6_tcp_channels.erase(i);
    }
}

#endif /* INET6 */

bool classifier::post_packet(mbuf* m)
{
    WITH_LOCK(osv::rcu_read_lock) {
        if (auto nc = classify_packet(m)) {
            log_packet_in(m, NETISR_ETHER);
            if (!nc->push(m)) {
                return false;
            }
            // FIXME: find a way to batch wakes
            nc->wake();
            return true;
        }
    }
    return false;
}

// must be called with rcu lock held
net_channel* classifier::classify_packet(mbuf* m)
{
    if (unsigned(m->m_hdr.mh_len) < ETHER_HDR_LEN)
        return nullptr;
    auto ether_hdr = mtod(m, struct ether_header*);
    uint8_t *payload = (uint8_t *)(ether_hdr) + sizeof(*ether_hdr);
    size_t payload_size = m->m_hdr.mh_len - sizeof(*ether_hdr);
    switch(ntohs(ether_hdr->ether_type)) {
    case ETHERTYPE_IP:
        return classify_ipv4_tcp(m, reinterpret_cast<ip*>(payload), payload_size);
#ifdef INET6
    case ETHERTYPE_IPV6:
        return classify_ipv6_tcp(m, reinterpret_cast<ip6_hdr*>(payload), payload_size);
#endif
    default:
        return nullptr;
    }
}

// must be called with rcu lock held
net_channel* classifier::classify_ipv4_tcp(mbuf* m, ip *ip_hdr, size_t ip_len)
{
    if (ip_len < sizeof(*ip_hdr)) {
        return nullptr;
    }
    unsigned ip_hdr_len = ip_hdr->ip_hl << 2;
    if (ip_hdr_len < sizeof(ip)) {
        return nullptr;
    }
    if (ip_hdr->ip_p != IPPROTO_TCP) {
        return nullptr;
    }
    if (ntohs(ip_hdr->ip_off) & ~IP_DF) {
        return nullptr;
    }

    auto tcp_hdr = reinterpret_cast<tcphdr*>(reinterpret_cast<uint8_t *>(ip_hdr) + ip_hdr_len);
    if (tcp_hdr->th_flags & (TH_SYN | TH_FIN | TH_RST)) {
	    return nullptr;
    }
    ipv4_tcp_conn_id id;
    id.src_addr = ip_hdr->ip_src;
    id.dst_addr = ip_hdr->ip_dst;
    id.src_port = ntohs(tcp_hdr->th_sport);
    id.dst_port = ntohs(tcp_hdr->th_dport);
    auto i = _ipv4_tcp_channels.reader_find(id,
            std::hash<ipv4_tcp_conn_id>(), key_item_compare<ipv4_tcp_conn_id>());
    if (!i) {
        return nullptr;
    }
    return i->chan;
}

#ifdef INET6

/* get offset for the last header in the chain unless packet is fragemented.
 * m_will be kept untainted.
 *
 * netchannel code doesn't handle fragmented packets, so these need to go
 * on the slow path.
 *
 * This code is based on the FreeBSD ip6_lasthdr() function.
 */
static int
ip6_lasthdr_nofrag(struct mbuf *m, int off, int proto, int *nxtp)
{
    int newoff;
    int nxt;

    if (!nxtp) {
        nxt = -1;
        nxtp = &nxt;
    }
    while (1) {
        newoff = ip6_nexthdr(m, off, proto, nxtp);
        if (newoff < 0)
            return off;
        else if (newoff < off)
            return -1;      /* invalid */
        else if (newoff == off)
            return newoff;

        off = newoff;
        proto = *nxtp;

        if (proto == IPPROTO_FRAGMENT)
             return -1;
    }
}


// must be called with rcu lock held
net_channel* classifier::classify_ipv6_tcp(mbuf* m, ip6_hdr *ip_hdr, size_t ip_len)
{
    int nxt;
    int nxt_off;
    int ip_off;
    uint8_t *start;

    if (ip_len < sizeof(*ip_hdr)) {
        return nullptr;
    }
    start = mtod(m, uint8_t*);
    ip_off = (uintptr_t)(reinterpret_cast<uint8_t*>(ip_hdr) - start);
    nxt_off = ip6_lasthdr_nofrag(m, ip_off, IPPROTO_IPV6, &nxt);
    if (nxt_off < 0 || nxt != IPPROTO_TCP) {
        return nullptr;
    }

    auto tcp_hdr = reinterpret_cast<tcphdr*>(start + nxt_off);
    if (tcp_hdr->th_flags & (TH_SYN | TH_FIN | TH_RST)) {
	    return nullptr;
    }

    ipv6_tcp_conn_id id;
    id.src_addr = ip_hdr->ip6_src;
    id.dst_addr = ip_hdr->ip6_dst;
    id.src_port = ntohs(tcp_hdr->th_sport);
    id.dst_port = ntohs(tcp_hdr->th_dport);

    auto i = _ipv6_tcp_channels.reader_find(id,
            std::hash<ipv6_tcp_conn_id>(), key_item_compare<ipv6_tcp_conn_id>());
    if (!i) {
        return nullptr;
    }
    return i->chan;
}

#endif /* INET6 */

