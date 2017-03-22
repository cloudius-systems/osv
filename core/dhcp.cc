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
#include <machine/in_cksum.h>

#include <bsd/porting/networking.hh>
#include <bsd/porting/route.h>

#include <osv/debug.hh>
#include <osv/dhcp.hh>
#include <osv/clock.hh>
#include <libc/network/__dns.hh>

using namespace boost::asio;

dhcp::dhcp_worker net_dhcp_worker;

u8 requested_options[] = {
    dhcp::DHCP_OPTION_SUBNET_MASK,
    dhcp::DHCP_OPTION_ROUTER,
    dhcp::DHCP_OPTION_DOMAIN_NAME_SERVERS,
    dhcp::DHCP_OPTION_INTERFACE_MTU,
    dhcp::DHCP_OPTION_BROADCAST_ADDRESS,
    dhcp::DHCP_OPTION_HOSTNAME
};

const ip::address_v4 ipv4_zero = ip::address_v4::address_v4::from_string("0.0.0.0");

// Returns whether we hooked the packet
int dhcp_hook_rx(struct mbuf* m)
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
    net_dhcp_worker.init();
    net_dhcp_worker.start(wait);
}

// Send DHCP release, for example at shutdown.
void dhcp_release()
{
    net_dhcp_worker.release();
}

void dhcp_renew(bool wait)
{
    net_dhcp_worker.renew(wait);
}

namespace dhcp {

    const char * dhcp_options_magic = "\x63\x82\x53\x63";

    static std::map<dhcp_message_type,const char*> dhcp_message_type_name_by_id = {
        {DHCP_MT_DISCOVER, "DHCPDISCOVER"},
        {DHCP_MT_OFFER, "DHCPOFFER"},
        {DHCP_MT_REQUEST, "DHCPREQUEST"},
        {DHCP_MT_DECLINE, "DHCPDECLINE"},
        {DHCP_MT_ACK, "DHCPACK"},
        {DHCP_MT_NAK, "DHCPNAK"},
        {DHCP_MT_RELEASE, "DHCPRELEASE"},
        {DHCP_MT_INFORM, "DHCPINFORM"},
        {DHCP_MT_LEASEQUERY, "DHCPLEASEQUERY"},
        {DHCP_MT_LEASEUNASSIGNED, "DHCPLEASEUNASSIGNED"},
        {DHCP_MT_LEASEUNKNOWN, "DHCPLEASEUNKNOWN"},
        {DHCP_MT_LEASEACTIVE, "DHCPLEASEACTIVE"},
        {DHCP_MT_INVALID, "DHCPINVALID"}
    };

    ///////////////////////////////////////////////////////////////////////////

    bool dhcp_socket::dhcp_send(dhcp_mbuf& packet)
    {
        struct bsd_sockaddr dst = {};
        struct mbuf *m = packet.get();

        dst.sa_family = AF_INET;
        dst.sa_len = 2;

        m->m_hdr.mh_flags |= M_BCAST;

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

        if (_m->m_hdr.mh_len < _ip_len + dhcp::udp_len + dhcp::min_dhcp_len) {
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

        // Header
        srand((unsigned int)
                osv::clock::wall::now().time_since_epoch().count());
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
        build_udp_ip_headers(dhcp_len, INADDR_ANY, INADDR_BROADCAST);
    }

    void dhcp_mbuf::compose_request(struct ifnet* ifp,
                                    u32 xid,
                                    ip::address_v4 yip,
                                    ip::address_v4 sip,
                                    dhcp_request_packet_type request_packet_type)
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
        ulong yip_n = htonl(yip.to_ulong());
        ulong sip_n = htonl(sip.to_ulong());
        if(request_packet_type == DHCP_REQUEST_RENEWING || request_packet_type == DHCP_REQUEST_REBINDING) {
            // ciaddr should only be set to if RENEWING or REBINDING
            // See pages 21 and 30-31 in https://www.ietf.org/rfc/rfc2131.txt
            memcpy(&pkt->ciaddr.s_addr, &yip_n, 4);
        }

        // Options
        u8* options_start = reinterpret_cast<u8*>(pkt+1);
        u8* options = options_start;
        memcpy(options, dhcp_options_magic, 4);
        options += 4;

        ip::address_v4::bytes_type dhcp_server_ip = sip.to_bytes();
        ip::address_v4::bytes_type requested_ip = yip.to_bytes();
        options = add_option(options, DHCP_OPTION_MESSAGE_TYPE, 1, DHCP_MT_REQUEST);
        options = add_option(options, DHCP_OPTION_DHCP_SERVER, 4, (u8*)&dhcp_server_ip);
        char hostname[256];
        if (0 == gethostname(hostname, sizeof(hostname))) {
            options = add_option(options, DHCP_OPTION_HOSTNAME, strlen(hostname), (u8*)hostname);
        }
        options = add_option(options, DHCP_OPTION_REQUESTED_ADDRESS, 4, (u8*)&requested_ip);
        options = add_option(options, DHCP_OPTION_PARAMETER_REQUEST_LIST,
            sizeof(requested_options), requested_options);
        *options++ = DHCP_OPTION_END;

        dhcp_len += options - options_start;

        // See page 33 in https://www.ietf.org/rfc/rfc2131.txt
        if(request_packet_type == DHCP_REQUEST_RENEWING) {
            build_udp_ip_headers(dhcp_len, yip_n, sip_n);
        }
        else {
            build_udp_ip_headers(dhcp_len, INADDR_ANY, INADDR_BROADCAST);
        }
    }

    void dhcp_mbuf::compose_release(struct ifnet* ifp,
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
        // Linux dhclient also uses new xid for release.
        pkt->xid = rand();
        pkt->secs = 0;
        pkt->flags = 0;
        memcpy(pkt->chaddr, IF_LLADDR(ifp), ETHER_ADDR_LEN);
        ulong yip_n = htonl(yip.to_ulong());
        ulong sip_n = htonl(sip.to_ulong());
        memcpy(&pkt->ciaddr.s_addr, &yip_n, 4);

        // Options
        u8* options_start = reinterpret_cast<u8*>(pkt+1);
        u8* options = options_start;
        memcpy(options, dhcp_options_magic, 4);
        options += 4;

        options = add_option(options, DHCP_OPTION_MESSAGE_TYPE, 1, DHCP_MT_RELEASE);
        options = add_option(options, DHCP_OPTION_DHCP_SERVER, 4, reinterpret_cast<u8*>(&sip_n));
        *options++ = DHCP_OPTION_END;

        dhcp_len += options - options_start;
        build_udp_ip_headers(dhcp_len, yip_n, sip_n);
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

#define PARSE_OP(type, cast, var) do {              \
    assert(op_len >= (sizeof(cast)));               \
    var = type(*(cast *)(options));                 \
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
        u8* limit = packet_start + _m->m_hdr.mh_len;
        u8* options = poptions();

        // Skip magic
        options += 4;

        while (options < limit) {
            dhcp_option_code op = dhcp_option_code(*options++);
            if (op == DHCP_OPTION_END) {
                break;
            }

            if (op == DHCP_OPTION_PAD) {
                continue;
            }

            assert(options < limit);

            u8 op_len = *options++;

            assert(options + op_len <= limit);

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
            case DHCP_OPTION_CLASSLESS_ROUTE:
                while (op_len) {
                    u8 mask;
                    PARSE_OP(u8, u8, mask);
                    options++; op_len--;
                    u8 len = (mask + 7) / 8;
                    u32 net = 0;
                    for (int i = 0; i < len; i++) {
                        u8 byte;
                        PARSE_OP(u8, u8, byte);
                        options++; op_len--;
                        net |= (byte << 8*i);
                    }
                    PARSE_OP(ip::address_v4::bytes_type,
                             ip::address_v4::bytes_type,
                             bytes);
                    options += 4; op_len -= 4;
                    _routes.emplace_back(ip::address_v4(ntohl(net)), ip::address_v4(u32(((1ull<<mask)-1) << (32-mask))), ip::address_v4(bytes));
                }
                break;
            case DHCP_OPTION_HOSTNAME:
                char hostname[256];
                memcpy(hostname, options, op_len);
                hostname[op_len] = '\0'; // terminating null
                dhcp_i( "DHCP received hostname: %s\n", hostname);
                _hostname = hostname;
                break;
            default:
                break;
            }

            options += op_len;
        }

        dhcp_i( "Received %s message from DHCP server: %s regarding offerred IP address: %s",
                dhcp_message_type_name_by_id[_message_type], _dhcp_server_ip.to_string().c_str(),
                _your_ip.to_string().c_str());
        return true;
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

    void dhcp_mbuf::build_udp_ip_headers(size_t dhcp_len, in_addr_t src_addr, in_addr_t dest_addr)
    {
        struct ip* ip = pip();
        struct udphdr* udp = pudp();

        // Set length in mbuf
        _m->M_dat.MH.MH_pkthdr.len = _m->m_hdr.mh_len = min_ip_len + udp_len + dhcp_len;

        // IP
        memset(ip, 0, sizeof(*ip));
        ip->ip_v = IPVERSION;
        ip->ip_hl = min_ip_len >> 2;
        ip->ip_len = htons(min_ip_len + udp_len + dhcp_len);
        ip->ip_id = 0;
        ip->ip_ttl = 128;
        ip->ip_p = IPPROTO_UDP;
        ip->ip_sum = 0;
        ip->ip_src.s_addr = src_addr;
        ip->ip_dst.s_addr = dest_addr;
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
        _client_addr = _server_addr = ipv4_zero;
    }

    dhcp_interface_state::~dhcp_interface_state()
    {
        delete _sock;
    }

    void dhcp_interface_state::discover()
    {
        // Update state
        _state = DHCP_DISCOVER;

        // Compose a dhcp discover packet
        dhcp_mbuf dm(false);
        dm.compose_discover(_ifp);

        // Save transaction id & send
        _xid = dm.get_xid();
        _client_addr = _server_addr = ipv4_zero;
        dhcp_i( "Broadcasting DHCPDISCOVER message with xid: [%d]",_xid);
        _sock->dhcp_send(dm);
    }

    void dhcp_interface_state::release()
    {
        // Update state
        _state = DHCP_INIT;

        // Compose a dhcp release packet
        dhcp_mbuf dm(false);
        dm.compose_release(_ifp, _client_addr, _server_addr);

        // Save transaction id & send
        _xid = dm.get_xid();
        dhcp_i( "Unicasting DHCPRELEASE message with xid: [%d] from client: %s to server: %s",
                _xid, _client_addr.to_string().c_str(), _server_addr.to_string().c_str());
        _sock->dhcp_send(dm);
        // IP and routes have to be removed
        osv::stop_if(_ifp->if_xname, _client_addr.to_string().c_str());
        // Here we assume that all DNS resolvers were added by this iface.
        // This might not be true if we have more than one iface.
        osv::set_dns_config({}, {});
        // no reply/ack is expected, after send we just forget all old state
        _client_addr = _server_addr = ipv4_zero;
    }

    void dhcp_interface_state::renew()
    {
        // Update state
        _state = DHCP_REQUEST;

        // Compose a dhcp request packet
        dhcp_mbuf dm(false);
        _xid = rand();
        dm.compose_request(_ifp,
                           _xid,
                           _client_addr,
                           _server_addr,
                           dhcp_mbuf::DHCP_REQUEST_RENEWING);

        // Send
        dhcp_i( "Unicasting DHCPREQUEST message with xid: [%d] from client: %s to server: %s in order to RENEW lease of: %s",
                _xid, _client_addr.to_string().c_str(), _server_addr.to_string().c_str(), _client_addr.to_string().c_str());
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

        // Send a DHCP Request
        _state = DHCP_REQUEST;
        dhcp_mbuf dm_req(false);
        dm_req.compose_request(_ifp,
                               _xid,
                               dm.get_your_ip(),
                               dm.get_dhcp_server_ip(),
                               dhcp_mbuf::DHCP_REQUEST_SELECTING);
        dhcp_i( "Broadcasting DHCPREQUEST message with xid: [%d] to SELECT offered IP: %s",
                _xid, dm.get_your_ip().to_string().c_str());
        _sock->dhcp_send(dm_req);
    }

    void dhcp_interface_state::state_request(dhcp_mbuf &dm)
    {
        if (dm.get_message_type() == DHCP_MT_ACK) {
            dhcp_i("Server acknowledged IP %s for interface %s with time to lease in seconds: %d",
                   dm.get_your_ip().to_string().c_str(), _ifp->if_xname, dm.get_lease_time_sec());
            _state = DHCP_ACKNOWLEDGE;
            _client_addr = dm.get_your_ip();
            _server_addr = dm.get_dhcp_server_ip();

            // TODO: check that the IP address is not responding with ARP
            // RFC2131 section 3.1.5

            printf("%s: %s\n",
                _ifp->if_xname,
                 dm.get_your_ip().to_string().c_str());
            dhcp_i("Configuring %s: ip %s subnet mask %s gateway %s MTU %d",
                _ifp->if_xname,
                 dm.get_your_ip().to_string().c_str(),
                 dm.get_subnet_mask().to_string().c_str(),
                 dm.get_router_ip().to_string().c_str(),
                 dm.get_interface_mtu() != 0 ? dm.get_interface_mtu() : ETHERMTU);

            if (dm.get_interface_mtu() != 0) {
                osv::if_set_mtu(_ifp->if_xname, dm.get_interface_mtu());
            }
            osv::start_if(_ifp->if_xname,
                          dm.get_your_ip().to_string().c_str(),
                          dm.get_subnet_mask().to_string().c_str());

            if (dm.get_subnet_mask() == ip::address_v4({0xff, 0xff, 0xff, 0xff})) {
                osv_route_add_interface(dm.get_router_ip().to_string().c_str(), nullptr, _ifp->if_xname);
            }
            osv_route_add_network("0.0.0.0",
                                  "0.0.0.0",
                                  dm.get_router_ip().to_string().c_str());

            std::for_each(dm.get_routes().begin(), dm.get_routes().end(), [&] (dhcp_mbuf::route_type& r) {
                auto dst = std::get<0>(r);
                auto mask = std::get<1>(r);
                auto gw = std::get<2>(r);

                dhcp_i("adding route: %s/%s -> %s", dst.to_string().c_str(), mask.to_string().c_str(), gw.to_string().c_str());

                if (gw == ip::address_v4::any()) {
                    osv_route_add_interface(dst.to_string().c_str(), mask.to_string().c_str(),
                            _ifp->if_xname);
                } else {
                    osv_route_add_network(dst.to_string().c_str(), mask.to_string().c_str(),
                            gw.to_string().c_str());
                }
            });

            osv::set_dns_config(dm.get_dns_ips(), std::vector<std::string>());
            if (dm.get_hostname().size()) {
	        sethostname(dm.get_hostname().c_str(), dm.get_hostname().size());
            }
            // TODO: setup lease
        } else if (dm.get_message_type() == DHCP_MT_NAK) {
            // from RFC 2131 section 3.1.5
            // "If the client receives a DHCPNAK message, the client restarts the
            // configuration process."
            _state = DHCP_INIT;
            _client_addr = _server_addr = ipv4_zero;
            discover();
        }
        // FIXME: retry on timeout and restart DORA sequence if it timeout a
        //        couple of time
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

    void dhcp_worker::init()
    {
        struct ifnet *ifp = nullptr;

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
        _dhcp_thread = sched::thread::make([&] { dhcp_worker_fn(); });
        _dhcp_thread->set_name("dhcp");
        _dhcp_thread->start();
    }

    void dhcp_worker::_send_and_wait(bool wait, dhcp_interface_state_send_packet iface_func)
    {
        // When doing renew, we still have IP, but want to reuse the flag.
        _have_ip = false;
        do {
            // Send discover or renew packets!
            for (auto &it: _universe) {
                (it.second->*iface_func)();
            }

            if (wait) {
                dhcp_i("Waiting for IP...");
                _waiter = sched::thread::current();

                sched::timer t(*sched::thread::current());
                using namespace osv::clock::literals;
                t.set(3_s);

                sched::thread::wait_until([&]{ return _have_ip || t.expired(); });
                _waiter = nullptr;
            }
        } while (!_have_ip && wait);
    }

    void dhcp_worker::start(bool wait)
    {
        // FIXME: clear routing table (use case run dhclient 2nd time)
        _send_and_wait(wait, &dhcp_interface_state::discover);
    }

    void dhcp_worker::release()
    {
        for (auto &it: _universe) {
            it.second->release();
        }
        _have_ip = false;
        // Wait a bit, so hopefully UDP release packets will be actually put on wire.
        usleep(1000);
    }

    void dhcp_worker::renew(bool wait)
    {
        _send_and_wait(wait, &dhcp_interface_state::renew);
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

            auto it = _universe.find(m->M_dat.MH.MH_pkthdr.rcvif);
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
        if (!_dhcp_thread) {
            /*
            With staticaly assigned IP, dhcp_worker::init() isn't called,
            and (injected) packets can/should be ignored.
            */
            dhcp_w("Ignoring inbound packet");
            return;
        }

        WITH_LOCK (_lock) {
            _rx_packets.push_front(m);
        }

        _dhcp_thread->wake();
    }

}
