/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/kernel_config_networking_dhcp6.h>
#if CONF_networking_dhcp6

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/netinet6/in6.h>
#include <bsd/sys/netinet6/in6_var.h>
#include <bsd/sys/netinet6/ip6_var.h>
#include <bsd/sys/netinet/ip6.h>
#include <bsd/sys/netinet6/scope6_var.h>
#include <bsd/sys/netinet/udp.h>
#include <osv/dhcp6.hh>
#include <osv/clock.hh>
#include <bsd/porting/networking.hh>
#include <cstring>
#include <algorithm>
#include <chrono>

// if_add_ipv6_addr() lives in the osv namespace glue layer; the DNS setter is
// a C-linkage helper implemented in a boost-friendly TU (see __dns.cc).
namespace osv {
    int if_add_ipv6_addr(std::string if_name, std::string ip_addr, std::string netmask);
}
extern "C" void osv_set_dns_config_str(const char* const* servers, int n);

// in6_cksum() is declared in <bsd/sys/netinet6/in6.h>; do not redeclare it.

namespace dhcp6 {

static dhcp6_worker net_dhcp6_worker;

// ---- big-endian option helpers ------------------------------------------

static void put16(std::vector<u8>& v, u16 x)
{
    v.push_back(x >> 8);
    v.push_back(x & 0xff);
}
static void put32(std::vector<u8>& v, u32 x)
{
    v.push_back((x >> 24) & 0xff);
    v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 8) & 0xff);
    v.push_back(x & 0xff);
}
static u16 get16(const u8* p) { return (p[0] << 8) | p[1]; }
static u32 get32(const u8* p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

// Append a DHCPv6 option (code, len, data) to a buffer.
static void put_option(std::vector<u8>& v, u16 code, const u8* data, u16 len)
{
    put16(v, code);
    put16(v, len);
    if (len)
        v.insert(v.end(), data, data + len);
}

// ---- interface helpers ---------------------------------------------------

// Fetch the interface MAC (6 bytes) into out; returns false if unavailable.
static bool if_mac(struct ifnet* ifp, u8 out[6])
{
    struct bsd_ifaddr* ifa;
    IF_ADDR_RLOCK(ifp);
    TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
        if (ifa->ifa_addr->sa_family == AF_LINK) {
            struct bsd_sockaddr_dl* sdl = (struct bsd_sockaddr_dl*)ifa->ifa_addr;
            if (sdl->sdl_alen == 6) {
                memcpy(out, LLADDR(sdl), 6);
                IF_ADDR_RUNLOCK(ifp);
                return true;
            }
        }
    }
    IF_ADDR_RUNLOCK(ifp);
    return false;
}

// Our link-local source address on the interface, for the IPv6 header.
static bool if_linklocal(struct ifnet* ifp, struct in6_addr* out)
{
    struct in6_ifaddr* ia = in6ifa_ifpforlinklocal(ifp, 0);
    if (!ia)
        return false;
    *out = ia->ia_addr.sin6_addr;
    ifa_free(&ia->ia_ifa);
    return true;
}

// ---- dhcp6_interface_state ----------------------------------------------

dhcp6_interface_state::dhcp6_interface_state(struct ifnet* ifp)
    : _ifp(ifp), _state(DH6_INIT), _prefixlen(64), _have_addr(false)
{
    // Transaction id: 24-bit random-ish from the clock.
    auto now = osv::clock::uptime::now().time_since_epoch().count();
    _xid = (u32)(now) & 0xffffff;
    _iaid = (u32)(now >> 8) & 0xffffffff;

    // Build a DUID-LLT (RFC 8415 11.2): type 1, hw type 1 (ethernet),
    // 32-bit time, then the link-layer address.
    u8 mac[6] = {0};
    if_mac(ifp, mac);
    put16(_duid, 1);                       // DUID-LLT
    put16(_duid, 1);                       // hardware type: ethernet
    put32(_duid, (u32)(now & 0xffffffff)); // time
    _duid.insert(_duid.end(), mac, mac + 6);

    memset(&_addr, 0, sizeof(_addr));
    _pref_lifetime = _valid_lifetime = 0;
}

void dhcp6_interface_state::solicit()
{
    _state = DH6_SOLICITING;
    send_message(DH6_SOLICIT, false /*server-id*/, false /*iaaddr*/);
}

void dhcp6_interface_state::request()
{
    _state = DH6_REQUESTING;
    send_message(DH6_REQUEST, true /*server-id*/, _have_addr /*iaaddr*/);
}

void dhcp6_interface_state::send_message(msg_type type, bool include_server_id,
                                         bool include_iaaddr)
{
    struct in6_addr src;
    if (!if_linklocal(_ifp, &src)) {
        dhcp6_w("dhcp6: no link-local source on %s yet", _ifp->if_xname);
        return;
    }

    // Build the DHCPv6 message body (after the 4-byte msg-type+xid header).
    std::vector<u8> body;
    body.push_back(type);
    body.push_back((_xid >> 16) & 0xff);
    body.push_back((_xid >> 8) & 0xff);
    body.push_back(_xid & 0xff);

    // Client Identifier.
    put_option(body, D6O_CLIENTID, _duid.data(), _duid.size());
    // Server Identifier (echo the one from ADVERTISE) on REQUEST.
    if (include_server_id && !_server_duid.empty())
        put_option(body, D6O_SERVERID, _server_duid.data(), _server_duid.size());

    // Rapid Commit (RFC 8415 21.14): included on SOLICIT to let the server
    // answer with a REPLY directly and skip ADVERTISE/REQUEST. Servers that do
    // not support it (or are configured not to) simply fall back to the normal
    // four-message exchange, which we also handle, so this is safe everywhere
    // (standard isc-dhcp/kea/dnsmasq as well as cloud providers like AWS).
    if (type == DH6_SOLICIT)
        put_option(body, D6O_RAPID_COMMIT, nullptr, 0);

    // Elapsed time (0).
    u8 elapsed[2] = {0, 0};
    put_option(body, D6O_ELAPSED_TIME, elapsed, 2);

    // IA_NA: iaid + T1 + T2 (+ optional IAADDR sub-option on REQUEST).
    std::vector<u8> iana;
    put32(iana, _iaid);
    put32(iana, 0);   // T1 - let server decide
    put32(iana, 0);   // T2
    if (include_iaaddr && _have_addr) {
        std::vector<u8> iaaddr;
        iaaddr.insert(iaaddr.end(), (u8*)&_addr, (u8*)&_addr + 16);
        put32(iaaddr, _pref_lifetime);
        put32(iaaddr, _valid_lifetime);
        put_option(iana, D6O_IAADDR, iaaddr.data(), iaaddr.size());
    }
    put_option(body, D6O_IA_NA, iana.data(), iana.size());

    // Option Request: ask for DNS servers.
    u8 oro[2];
    oro[0] = (D6O_DNS_SERVERS >> 8) & 0xff;
    oro[1] = D6O_DNS_SERVERS & 0xff;
    put_option(body, D6O_ORO, oro, 2);

    // Wrap in IPv6/UDP and hand to ip6_output (handles multicast MAC +
    // checksum + routing, like nd6_rs_output).
    int udplen = sizeof(struct udphdr) + body.size();
    int totlen = sizeof(struct ip6_hdr) + udplen;

    struct mbuf* m;
    MGETHDR(m, M_DONTWAIT, MT_DATA);
    if (!m)
        return;
    if (totlen + max_linkhdr >= (int)MHLEN) {
        MCLGET(m, M_DONTWAIT);
        if ((m->m_hdr.mh_flags & M_EXT) == 0) {
            m_free(m);
            return;
        }
    }
    m->M_dat.MH.MH_pkthdr.rcvif = NULL;
    m->M_dat.MH.MH_pkthdr.len = m->m_hdr.mh_len = totlen;
    m->m_hdr.mh_data += max_linkhdr;

    struct ip6_hdr* ip6 = mtod(m, struct ip6_hdr*);
    memset(ip6, 0, sizeof(*ip6));
    ip6->ip6_vfc = IPV6_VERSION;
    ip6->ip6_nxt = IPPROTO_UDP;
    ip6->ip6_hlim = 255;
    ip6->ip6_plen = htons(udplen);
    ip6->ip6_src = src;
    // ff02::1:2 - All_DHCP_Relay_Agents_and_Servers.
    memset(&ip6->ip6_dst, 0, sizeof(ip6->ip6_dst));
    ip6->ip6_dst.s6_addr[0]  = 0xff;
    ip6->ip6_dst.s6_addr[1]  = 0x02;
    ip6->ip6_dst.s6_addr[13] = 0x01;
    ip6->ip6_dst.s6_addr[15] = 0x02;
    // Set the multicast scope zone to this interface, otherwise ip6_output
    // cannot pick an output interface for the link-local multicast and the
    // packet is silently dropped (same requirement as nd6_rs_output).
    if (in6_setscope(IP6_HDR_FIELD_ADDR(ip6, ip6_dst, in6_addr), _ifp, NULL) != 0) {
        m_freem(m);
        return;
    }

    struct udphdr* uh = (struct udphdr*)(ip6 + 1);
    uh->uh_sport = htons(client_port);
    uh->uh_dport = htons(server_port);
    uh->uh_ulen  = htons(udplen);
    uh->uh_sum   = 0;
    memcpy((u8*)(uh + 1), body.data(), body.size());

    // UDP checksum over the IPv6 pseudo-header.
    uh->uh_sum = in6_cksum(m, IPPROTO_UDP, sizeof(struct ip6_hdr), udplen);
    if (uh->uh_sum == 0)
        uh->uh_sum = 0xffff;

    struct ip6_moptions im6o;
    memset(&im6o, 0, sizeof(im6o));
    im6o.im6o_multicast_ifp = _ifp;
    im6o.im6o_multicast_hlim = 255;
    im6o.im6o_multicast_loop = 0;

    struct route_in6 ro;
    memset(&ro, 0, sizeof(ro));
    ip6_output(m, NULL, &ro, 0, &im6o, NULL, NULL);
    if (ro.ro_rt)
        RTFREE(ro.ro_rt);

    dhcp6_i("dhcp6: sent %s on %s",
            type == DH6_SOLICIT ? "SOLICIT" : "REQUEST", _ifp->if_xname);
}

// Parse a received DHCPv6 message payload (UDP body). Fills learned fields.
bool dhcp6_interface_state::parse(struct mbuf* m, msg_type& out_type)
{
    // The hook stashed the DHCPv6 payload offset (past ip6 + any extension
    // headers + udp) in csum_data; fall back to the plain-UDP layout if unset.
    int payload_off = (int)m->M_dat.MH.MH_pkthdr.csum_data;
    if (payload_off <= 0)
        payload_off = sizeof(struct ip6_hdr) + sizeof(struct udphdr);
    u8* p = mtod(m, u8*) + payload_off;
    int blen = m->M_dat.MH.MH_pkthdr.len - payload_off;
    if (blen < 4)
        return false;

    out_type = (msg_type)p[0];
    u32 xid = (p[1] << 16) | (p[2] << 8) | p[3];
    if (xid != _xid)
        return false;   // not our transaction

    int off = 4;
    while (off + 4 <= blen) {
        u16 code = get16(p + off);
        u16 len = get16(p + off + 2);
        off += 4;
        if (off + len > blen)
            break;
        const u8* d = p + off;
        switch (code) {
        case D6O_SERVERID:
            _server_duid.assign(d, d + len);
            break;
        case D6O_IA_NA: {
            // iaid(4) T1(4) T2(4) then sub-options (IAADDR / STATUS_CODE).
            if (len < 12)
                break;
            int so = 12;
            while (so + 4 <= (int)len) {
                u16 sc = get16(d + so);
                u16 sl = get16(d + so + 2);
                so += 4;
                if (so + sl > (int)len)
                    break;
                if (sc == D6O_IAADDR && sl >= 24) {
                    memcpy(&_addr, d + so, 16);
                    _pref_lifetime = get32(d + so + 16);
                    _valid_lifetime = get32(d + so + 20);
                    _have_addr = (_valid_lifetime != 0);
                } else if (sc == D6O_STATUS_CODE && sl >= 2) {
                    if (get16(d + so) != DH6_STATUS_SUCCESS)
                        _have_addr = false;
                }
                so += sl;
            }
            break;
        }
        case D6O_DNS_SERVERS:
            for (int i = 0; i + 16 <= (int)len; i += 16) {
                char buf[INET6_ADDRSTRLEN];
                struct in6_addr a;
                memcpy(&a, d + i, 16);
                _dns.push_back(std::string(ip6_sprintf(buf, &a)));
            }
            break;
        default:
            break;
        }
        off += len;
    }
    return true;
}

void dhcp6_interface_state::bind_address()
{
    if (!_have_addr)
        return;
    char buf[INET6_ADDRSTRLEN];
    std::string astr(ip6_sprintf(buf, &_addr));
    // DHCPv6 IA_NA addresses are /128 host addresses on the link; the RA
    // supplied the on-link prefix, so use a /64 mask for typical VPC setups.
    std::string mask = "ffff:ffff:ffff:ffff::";
    if (osv::if_add_ipv6_addr(_ifp->if_xname, astr, mask) == 0) {
        dhcp6_i("dhcp6: bound %s/64 on %s", astr.c_str(), _ifp->if_xname);
    } else {
        dhcp6_w("dhcp6: failed to add %s on %s", astr.c_str(), _ifp->if_xname);
    }
    if (!_dns.empty()) {
        std::vector<const char*> ptrs;
        for (auto& s : _dns)
            ptrs.push_back(s.c_str());
        osv_set_dns_config_str(ptrs.data(), (int)ptrs.size());
    }
    _state = DH6_BOUND;
}

void dhcp6_interface_state::process_packet(struct mbuf* m)
{
    msg_type t;
    if (!parse(m, t))
        return;

    if (_state == DH6_SOLICITING) {
        if (t == DH6_REPLY && _have_addr) {
            // Rapid Commit (RFC 8415 18.2.1): the server answered our SOLICIT
            // with a REPLY directly, skipping ADVERTISE/REQUEST. AWS VPC does
            // this. Bind straight away.
            bind_address();
        } else if (t == DH6_ADVERTISE) {
            // Normal four-message exchange: confirm the offer with a REQUEST.
            request();
        }
    } else if (_state == DH6_REQUESTING && t == DH6_REPLY) {
        bind_address();
    }
}

// ---- dhcp6_worker --------------------------------------------------------

dhcp6_worker::dhcp6_worker()
    : _thread(nullptr), _bound(false), _waiter(nullptr) {}

dhcp6_worker::~dhcp6_worker()
{
    for (auto& kv : _universe)
        delete kv.second;
}

void dhcp6_worker::init()
{
    struct ifnet* ifp;
    IFNET_RLOCK_NOSLEEP();
    TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
        if (ifp->if_flags & IFF_LOOPBACK)
            continue;
        if (_universe.find(ifp) == _universe.end())
            _universe[ifp] = new dhcp6_interface_state(ifp);
    }
    IFNET_RUNLOCK_NOSLEEP();

    if (!_thread) {
        _thread = sched::thread::make([&] { dhcp6_worker_fn(); },
                                      sched::thread::attr().name("dhcp6"));
        _thread->start();
    }
}

void dhcp6_worker::queue_packet(struct mbuf* m)
{
    WITH_LOCK(_lock) {
        _rx_packets.push_back(m);
    }
    if (_thread)
        _thread->wake();
}

void dhcp6_worker::dhcp6_worker_fn()
{
    while (true) {
        struct mbuf* m = nullptr;
        WITH_LOCK(_lock) {
            if (!_rx_packets.empty()) {
                m = _rx_packets.front();
                _rx_packets.pop_front();
            }
        }
        if (!m) {
            sched::thread::wait_until([&] {
                WITH_LOCK(_lock) { return !_rx_packets.empty(); }
            });
            continue;
        }
        // Deliver to whichever interface state matches the receiving ifp. The
        // rcvif on a DHCPv6 reply should match a key in _universe; if it does
        // not (e.g. the mbuf lost its rcvif), fall back to the sole non-
        // loopback interface, which is the common unikernel case.
        struct ifnet* rcvif = m->M_dat.MH.MH_pkthdr.rcvif;
        auto it = _universe.find(rcvif);
        if (it == _universe.end() && _universe.size() == 1)
            it = _universe.begin();
        if (it != _universe.end()) {
            it->second->process_packet(m);
            if (it->second->is_bound() && !_bound) {
                _bound = true;
                if (_waiter)
                    _waiter->wake();
            }
        }
        m_freem(m);
    }
}

void dhcp6_worker::start(bool wait)
{
    // Kick off SOLICIT on every interface, then optionally wait for a bind.
    for (auto& kv : _universe)
        kv.second->solicit();

    if (!wait)
        return;

    _waiter = sched::thread::current();
    // Retransmit SOLICIT with RFC 8415-style backoff until we bind or give up.
    // Real servers (and cloud DHCPv6 like AWS VPC) can take a few seconds to a
    // few tens of seconds to answer, so keep at it for ~30s rather than a
    // handful of tries.
    int interval_ms = 1000;
    for (int elapsed = 0; elapsed < 15000 && !_bound; elapsed += interval_ms) {
        sched::timer t(*sched::thread::current());
        using namespace osv::clock::literals;
        t.set(std::chrono::milliseconds(interval_ms));
        sched::thread::wait_until([&] { return _bound || t.expired(); });
        if (!_bound) {
            for (auto& kv : _universe)
                if (!kv.second->is_bound())
                    kv.second->solicit();
            // RFC 8415 exponential backoff, capped at 4s between solicits.
            interval_ms = std::min(interval_ms * 2, 4000);
        }
    }
    _waiter = nullptr;
    if (!_bound)
        dhcp6_w("dhcp6: no address bound (no DHCPv6 server responded)");
}

} // namespace dhcp6

// RX hook, called from ip6_input for UDP packets to the DHCPv6 client port.
// Returns 1 if the packet was consumed.
// Called from udp6_input for a UDP datagram that matched no PCB. udp6_input has
// already validated the packet and located the UDP header at @off, so we do NOT
// re-parse ip6_nxt here (which would be wrong if the packet carried extension
// headers - the reason an earlier ip6_nxt-based check failed to consume replies
// from routers that prepend a Hop-by-Hop header).  Returns 1 if consumed.
extern "C" int dhcp6_hook_rx_at(struct mbuf* m, int off)
{
    struct udphdr* uh = (struct udphdr*)(mtod(m, caddr_t) + off);
    if (uh->uh_dport != htons(dhcp6::client_port))
        return 0;
    dhcp6_i("dhcp6: hook consumed a UDP/546 reply");
    // Stash the UDP payload offset so the worker can find the DHCPv6 message
    // regardless of any extension headers.
    m->M_dat.MH.MH_pkthdr.csum_data = off + sizeof(struct udphdr);
    dhcp6::net_dhcp6_worker.queue_packet(m);
    return 1;
}

// Legacy entry (kept for the ip6_input path / compatibility): re-derives from
// the start of the mbuf assuming a plain UDP packet with no extension headers.
extern "C" int dhcp6_hook_rx(struct mbuf* m)
{
    struct ip6_hdr* ip6 = mtod(m, struct ip6_hdr*);
    if (ip6->ip6_nxt != IPPROTO_UDP)
        return 0;
    if (m->m_hdr.mh_len < (int)(sizeof(struct ip6_hdr) + sizeof(struct udphdr)))
        return 0;
    return dhcp6_hook_rx_at(m, sizeof(struct ip6_hdr));
}

extern "C" void dhcp6_start(bool wait)
{
    dhcp6::net_dhcp6_worker.init();
    dhcp6::net_dhcp6_worker.start(wait);
}

#endif // CONF_networking_dhcp6
