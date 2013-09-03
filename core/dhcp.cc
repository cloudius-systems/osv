/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <list>

#include <stdlib.h>

#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/queue.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/udp.h>
#include <bsd/machine/in_cksum.h>

#include <bsd/porting/networking.h>
#include <bsd/porting/route.h>

#include <debug.hh>
#include <dhcp.hh>
#include <drivers/clock.hh>

namespace osv {
void set_dns_config(std::vector<boost::asio::ip::address> nameservers,
                    std::vector<std::string> search_domains);
}

using namespace boost::asio;

dhcp::dhcp_worker net_dhcp_worker;

// Returns whether we hooked the packet
extern "C" int dhcp_hook_rx(struct mbuf* m)
{
    dhcp::dhcp_mbuf dm(false, m);

    // Filter only valid dhcp packets
    if (!dm.is_valid_dhcp()) {
        return 0;
    }

    // Queue the packet
    net_dhcp_worker.queue_packet(m);

    return 1;
}

void dhcp_start(bool wait)
{
    // Initialize the global DHCP worker
    net_dhcp_worker.init(wait);
}

namespace dhcp {

    const char * dhcp_options_magic = "\x63\x82\x53\x63";

    ///////////////////////////////////////////////////////////////////////////

    bool dhcp_socket::dhcp_send(dhcp_mbuf& packet)
    {

        struct bsd_sockaddr dst = {};
        struct mbuf *m = packet.get();

        dst.sa_family = AF_INET;
        dst.sa_len = 2;

        m->m_flags |= M_BCAST;

        // Transmit the packet directly over Ethernet
        int c = _ifp->if_output(_ifp, packet.get(), &dst, NULL);

        return (c == 0);
    }

    ///////////////////////////////////////////////////////////////////////////

    dhcp_mbuf::dhcp_mbuf(bool attached, struct mbuf* m)
        : _attached(attached), _m(m), _ip_len(min_ip_len)
    {
        if (m == nullptr) {
            allocate_mbuf();
        }

        // Init decoded fields
        _message_type = DHCP_MT_INVALID;
        _lease_time_sec = 0;
        _renewal_time_sec = 0;
        _rebind_time_sec = 0;
        _mtu = 0;
    }

    dhcp_mbuf::~dhcp_mbuf()
    {
        if (_attached) {
            m_free(_m);
        }
    }

    void dhcp_mbuf::detach()
    {
        _attached = false;
    }

    struct mbuf* dhcp_mbuf::get()
    {
        return _m;
    }

    void dhcp_mbuf::set(struct mbuf* m)
    {
        _m = m;
    }

    bool dhcp_mbuf::is_valid_dhcp()
    {
        decode_ip_len();
        struct ip* ip = pip();
        struct udphdr* udp = pudp();
        u8* options = poptions();

        if (_m->m_len < _ip_len + dhcp::udp_len + dhcp::min_dhcp_len) {
            return false;
        }

        if ((ip->ip_p != IPPROTO_UDP) || (udp->uh_dport != ntohs(dhcp_client_port))) {
            return false;
        }

        // Check options magic
        if (memcmp(options, dhcp_options_magic, 4) != 0) {
            return false;
        }

        // FIXME: checksums

        return true;
    }

    void dhcp_mbuf::allocate_mbuf()
    {
        _m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MCLBYTES);
    }

    void dhcp_mbuf::compose_discover(struct ifnet* ifp)
    {
        size_t dhcp_len = sizeof(struct dhcp_packet);
        struct dhcp_packet* pkt = pdhcp();
        *pkt = {};
        u8 requested_options[] = {DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER,
            DHCP_OPTION_DOMAIN_NAME_SERVERS, DHCP_OPTION_INTERFACE_MTU,
            DHCP_OPTION_BROADCAST_ADDRESS};

        // Header
        srand(nanotime());
        pkt->op = BOOTREQUEST;
        pkt->htype = HTYPE_ETHERNET;
        pkt->hlen = ETHER_ADDR_LEN;
        pkt->hops = 0;
        pkt->xid = rand();
        pkt->secs = 0;
        pkt->flags = 0;
        memcpy(pkt->chaddr, IF_LLADDR(ifp), ETHER_ADDR_LEN);

        // Options
        u8* options_start = reinterpret_cast<u8*>(pkt+1);
        u8* options = options_start;
        memcpy(options, dhcp_options_magic, 4);
        options += 4;

        options = add_option(options, DHCP_OPTION_MESSAGE_TYPE, 1, DHCP_MT_DISCOVER);
        options = add_option(options, DHCP_OPTION_PARAMETER_REQUEST_LIST,
            sizeof(requested_options), requested_options);
        *options++ = DHCP_OPTION_END;

        dhcp_len += options - options_start;
        build_udp_ip_headers(dhcp_len);
    }

    void dhcp_mbuf::compose_request(struct ifnet* ifp,
                                    u32 xid,
                                    ip::address_v4 yip,
                                    ip::address_v4 sip)
    {
        size_t dhcp_len = sizeof(struct dhcp_packet);
        struct dhcp_packet* pkt = pdhcp();
        *pkt = {};

        // Header
        pkt->op = BOOTREQUEST;
        pkt->htype = HTYPE_ETHERNET;
        pkt->hlen = ETHER_ADDR_LEN;
        pkt->hops = 0;
        pkt->xid = xid;
        pkt->secs = 0;
        pkt->flags = 0;
        memcpy(pkt->chaddr, IF_LLADDR(ifp), ETHER_ADDR_LEN);

        // Options
        u8* options_start = reinterpret_cast<u8*>(pkt+1);
        u8* options = options_start;
        memcpy(options, dhcp_options_magic, 4);
        options += 4;

        ip::address_v4::bytes_type dhcp_server_ip = sip.to_bytes();
        ip::address_v4::bytes_type requested_ip = yip.to_bytes();
        options = add_option(options, DHCP_OPTION_MESSAGE_TYPE, 1, DHCP_MT_REQUEST);
        options = add_option(options, DHCP_OPTION_DHCP_SERVER, 4, (u8*)&dhcp_server_ip);
        options = add_option(options, DHCP_OPTION_REQUESTED_ADDRESS, 4, (u8*)&requested_ip);
        *options++ = DHCP_OPTION_END;

        dhcp_len += options - options_start;
        build_udp_ip_headers(dhcp_len);
    }

    u32 dhcp_mbuf::get_xid()
    {
        return (pdhcp()->xid);
    }

    void dhcp_mbuf::decode_ip_len()
    {
        struct ip* ip = pip();
        _ip_len = ip->ip_hl << 2;
    }

#define LENGTH_OK ((options - packet_start) + op_len + len_check_hdr < _m->m_len)
#define PARSE_OP(type, cast, var) do {              \
    if ((op_len >= (sizeof(cast))) && (LENGTH_OK))  \
        var = type(*(cast *)(options));             \
    else return false;                              \
} while(0);

    bool dhcp_mbuf::decode()
    {
        ip::address_v4::bytes_type bytes;

        decode_ip_len();

        // clear DNS
        _dns_ips.clear();

        // Read allocated IP address
        memcpy(&bytes, &pdhcp()->yiaddr.s_addr, sizeof(bytes));
        _your_ip = ip::address_v4(bytes);

        // Parse options
        u8* packet_start = mtod(_m, u8*);
        u8* options = poptions();

        // Skip magic
        options += 4;

        dhcp_option_code op = DHCP_OPTION_PAD;
        u8 op_len = 0;
        u8 len_check_hdr = 2;

        while (LENGTH_OK && (op != DHCP_OPTION_END)) {
            dhcp_option_code op = dhcp_option_code(*options++);
            op_len = *options++;

            len_check_hdr = 0;
            switch (op) {
            case DHCP_OPTION_MESSAGE_TYPE:
                PARSE_OP(dhcp_message_type, u8, _message_type);
                break;
            case DHCP_OPTION_SUBNET_MASK:
                PARSE_OP(ip::address_v4::bytes_type,
                         ip::address_v4::bytes_type,
                         bytes);
                _subnet_mask = ip::address_v4(bytes);
                break;
            case DHCP_OPTION_ROUTER:
                PARSE_OP(ip::address_v4::bytes_type,
                         ip::address_v4::bytes_type,
                         bytes);
                _router_ip = ip::address_v4(bytes);
                break;
            case DHCP_OPTION_DHCP_SERVER:
                PARSE_OP(ip::address_v4::bytes_type,
                         ip::address_v4::bytes_type,
                         bytes);
                _dhcp_server_ip = ip::address_v4(bytes);
                break;
            case DHCP_OPTION_DOMAIN_NAME_SERVERS:
                PARSE_OP(ip::address_v4::bytes_type,
                         ip::address_v4::bytes_type,
                         bytes);
                _dns_ips.push_back(ip::address(ip::address_v4(bytes)));
                break;
            case DHCP_OPTION_INTERFACE_MTU:
                PARSE_OP(u16, u16, _mtu);
                _mtu = ntohs(_mtu);
                break;
            case DHCP_OPTION_BROADCAST_ADDRESS:
                PARSE_OP(ip::address_v4::bytes_type,
                         ip::address_v4::bytes_type,
                         bytes);
                _broadcast_ip = ip::address_v4(bytes);
                break;
            case DHCP_OPTION_LEASE_TIME:
                PARSE_OP(u32, u32, _lease_time_sec);
                _lease_time_sec = ntohl(_lease_time_sec);
                break;
            case DHCP_OPTION_RENEWAL_TIME:
                PARSE_OP(u32, u32, _renewal_time_sec);
                _renewal_time_sec = ntohl(_renewal_time_sec);
                break;
            case DHCP_OPTION_REBINDING_TIME:
                PARSE_OP(u32, u32, _rebind_time_sec);
                _rebind_time_sec = ntohl(_rebind_time_sec);
                break;
            default:
                break;
            }

            options += op_len;
            len_check_hdr = 2;
        }

        return true;
    }

    u8* dhcp_mbuf::lookup_option(dhcp_option_code type, u8 *len)
    {
        u8* packet_start = mtod(_m, u8*);
        u8* options = poptions();

        // Skip magic
        options += 4;

        dhcp_option_code op = DHCP_OPTION_PAD;
        while (((options - packet_start) < _m->m_len) && (op != DHCP_OPTION_END)) {
            dhcp_option_code op = dhcp_option_code(*options++);
            u8 op_len = *options++;

            if ((op == type) && ((options - packet_start) + op_len < _m->m_len)) {
                *len = op_len;
                return (options);
            }

            options += op_len;
        }

        return nullptr;
    }

    struct ip* dhcp_mbuf::pip()
    {
        return mtod(_m, struct ip*);
    }

    struct udphdr* dhcp_mbuf::pudp()
    {
        return reinterpret_cast<struct udphdr*>(mtod(_m, u8*) + _ip_len);
    }

    struct dhcp_packet* dhcp_mbuf::pdhcp()
    {
        return reinterpret_cast<struct dhcp_packet*>(mtod(_m, u8*) + _ip_len + udp_len);
    }

    u8* dhcp_mbuf::poptions()
    {
        return (reinterpret_cast<u8*>(pdhcp()+1));
    }

    u8* dhcp_mbuf::add_option(u8* pos, u8 type, u8 len, u8* buf)
    {
        pos[0] = type;
        pos[1] = len;
        memcpy(&pos[2], buf, len);

        return pos + 2 + len;
    }

    u8* dhcp_mbuf::add_option(u8* pos, u8 type, u8 len, u8 data)
    {
        pos[0] = type;
        pos[1] = len;
        memset(&pos[2], data, len);

        return pos + 2 + len;
    }

    void dhcp_mbuf::build_udp_ip_headers(size_t dhcp_len)
    {
        struct ip* ip = pip();
        struct udphdr* udp = pudp();

        // Set length in mbuf
        _m->m_pkthdr.len = _m->m_len = min_ip_len + udp_len + dhcp_len;

        // IP
        memset(ip, 0, sizeof(*ip));
        ip->ip_v = IPVERSION;
        ip->ip_hl = min_ip_len >> 2;
        ip->ip_len = htons(min_ip_len + udp_len + dhcp_len);
        ip->ip_id = 0;
        ip->ip_ttl = 128;
        ip->ip_p = IPPROTO_UDP;
        ip->ip_sum = 0;
        ip->ip_src.s_addr = INADDR_ANY;
        ip->ip_dst.s_addr = INADDR_BROADCAST;
        ip->ip_sum = in_cksum(_m, min_ip_len);

        // UDP
        memset(udp, 0, sizeof(*udp));
        udp->uh_sport = htons(dhcp_client_port);
        udp->uh_dport = htons(dhcp_server_port);
        udp->uh_ulen = htons(udp_len + dhcp_len);
        // FIXME: add a "proper" UDP checksum,
        // in the meanwhile, 0 will work as the RFC allows it.
        udp->uh_sum = 0;
    }

    ///////////////////////////////////////////////////////////////////////////

    dhcp_interface_state::dhcp_interface_state(struct ifnet* ifp)
        : _state(DHCP_INIT), _ifp(ifp)
    {
        _sock = new dhcp_socket(ifp);
        _xid = 0;
    }

    dhcp_interface_state::~dhcp_interface_state()
    {
        delete _sock;
    }

    void dhcp_interface_state::discover()
    {
        // FIXME: send release packet in case the interface has an address

        // Update state
        _state = DHCP_DISCOVER;

        // Compose a dhcp discover packet
        dhcp_mbuf dm(false);
        dm.compose_discover(_ifp);

        // Save transaction id & send
        _xid = dm.get_xid();
        _sock->dhcp_send(dm);
    }

    void dhcp_interface_state::process_packet(struct mbuf* m)
    {
        dhcp_mbuf dm(true, m);

        if (!dm.decode()) {
            dhcp_w("Unable to decode DHCP packet");
            return;
        }

        // Validate transaction id
        if (dm.get_xid() != _xid) {
            dhcp_w("Got packet with wrong transaction ID (%d, %d)", _xid, dm.get_xid());
            return;
        }

        ///////////////////
        // State Machine //
        ///////////////////

        if (_state == DHCP_DISCOVER) {
            state_discover(dm);

        } else if (_state == DHCP_REQUEST) {
            state_request(dm);
        }

    }

    void dhcp_interface_state::state_discover(dhcp_mbuf &dm)
    {
        if (dm.get_message_type() != DHCP_MT_OFFER) {
            dhcp_w("Not offer packet in discover state, type = %d", dm.get_message_type());
            return;
        }

        dhcp_i("Configuring %s: ip %s subnet mask %s gateway %s",
            _ifp->if_xname,
             dm.get_your_ip().to_string().c_str(),
             dm.get_subnet_mask().to_string().c_str(),
             dm.get_router_ip().to_string().c_str());

        osv_start_if(_ifp->if_xname,
                     dm.get_your_ip().to_string().c_str(),
                     dm.get_subnet_mask().to_string().c_str());
        osv_route_add_network("0.0.0.0",
                              "0.0.0.0",
                              dm.get_router_ip().to_string().c_str());
        osv::set_dns_config(dm.get_dns_ips(), std::vector<std::string>());

        // Send a DHCP Request
        _state = DHCP_REQUEST;
        dhcp_mbuf dm_req(false);
        dm_req.compose_request(_ifp,
                               _xid,
                               dm.get_your_ip(),
                               dm.get_dhcp_server_ip());
        _sock->dhcp_send(dm_req);
    }

    void dhcp_interface_state::state_request(dhcp_mbuf &dm)
    {
        if (dm.get_message_type() == DHCP_MT_ACK) {
            dhcp_i("Server acknowledged IP for interface %s", _ifp->if_xname);
            _state = DHCP_ACKNOWLEDGE;

            // FIXME: if we get a nack or timeout, clear routing information
            // TODO: configure DNS
            // TODO: setup lease
        }
    }

    ///////////////////////////////////////////////////////////////////////////

    dhcp_worker::dhcp_worker()
        : _dhcp_thread(nullptr), _have_ip(false), _waiter(nullptr)
    {

    }

    dhcp_worker::~dhcp_worker()
    {
        if (_dhcp_thread) {
            delete _dhcp_thread;
        }

        // FIXME: free packets and states
    }

    void dhcp_worker::init(bool wait)
    {
        struct ifnet *ifp = nullptr;

        // FIXME: clear routing table (use case run dhclient 2nd time)

        // Allocate a state for each interface
        IFNET_RLOCK();
        TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
            if ( (!(ifp->if_flags & IFF_DYING)) &&
                 (!(ifp->if_flags & IFF_LOOPBACK)) ) {
                _universe.insert(std::make_pair(ifp,
                    new dhcp_interface_state(ifp)));
            }
        }
        IFNET_RUNLOCK();

        // Create the worker thread
        _dhcp_thread = new sched::thread([&] { dhcp_worker_fn(); });
        _dhcp_thread->start();

        // Send discover packets!
        for (auto &it: _universe) {
            it.second->discover();
        }

        if (wait) {
            dhcp_i("Waiting for IP...");
            _waiter = sched::thread::current();
            sched::thread::wait_until([&]{ return _have_ip; });
        }
    }

    void dhcp_worker::dhcp_worker_fn()
    {
        while (true) {
            mbuf* m;
            WITH_LOCK(_lock) {
                sched::thread::wait_until(_lock, [&] {
                    return (!_rx_packets.empty());
                });

                // Get packet for handling
                m = _rx_packets.front();
                _rx_packets.pop_front();
            }

            auto it = _universe.find(m->m_pkthdr.rcvif);
            if (it == _universe.end()) {
                dhcp_e("Couldn't find interface state for DHCP packet!");
                abort();
            }

            it->second->process_packet(m);

            // Check if we got an ip
            if (it->second->is_acknowledged()) {
                _have_ip = true;
                if (_waiter) {
                    _waiter->wake();
                }
            }
        }
    }

    void dhcp_worker::queue_packet(struct mbuf* m)
    {
        WITH_LOCK (_lock) {
            _rx_packets.push_front(m);
        }

        _dhcp_thread->wake();
    }

}
