/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __DHCP_HH__
#define __DHCP_HH__

#include <list>
#include <vector>

#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/udp.h>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/debug.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>

extern "C" void dhcp_start(bool wait);

namespace dhcp {

    #define dhcp_tag "dhcp"
    #define dhcp_d(...)   tprintf_d(dhcp_tag, __VA_ARGS__)
    #define dhcp_i(...)   tprintf_i(dhcp_tag, __VA_ARGS__)
    #define dhcp_w(...)   tprintf_w(dhcp_tag, __VA_ARGS__)
    #define dhcp_e(...)   tprintf_e(dhcp_tag, __VA_ARGS__)

    constexpr u_short dhcp_client_port = 68;
    constexpr u_short dhcp_server_port = 67;

    enum dhcp_type {
        BOOTREQUEST = 1,
        BOOTREPLY = 2
    };

    enum dhcp_hw_type {
        HTYPE_ETHERNET = 1
    };

    struct dhcp_packet {
        u8 op;                  // Message op code / message type.
        u8 htype;               // Hardware address type
        u8 hlen;                // Hardware address length
        u8 hops;                // Client sets to zero, used by relay agents
        u32 xid;                // Client sets Transaction ID, a random number
        u16 secs;               // Client sets seconds elapsed since op start
        u16 flags;              // Flags
        struct in_addr ciaddr;  // Client IP address
        struct in_addr yiaddr;  // 'your' (client) IP address.
        struct in_addr siaddr;  // IP address of next server to use in bootstrap
        struct in_addr giaddr;  // Relay agent IP address
        u8 chaddr[16];          // Client hardware address.
        char sname[64];         // unused
        char file[128];         // unused
    };

    enum dhcp_option_code {
        DHCP_OPTION_PAD = 0,
        DHCP_OPTION_SUBNET_MASK = 1,
        DHCP_OPTION_ROUTER = 3,
        DHCP_OPTION_DOMAIN_NAME_SERVERS = 6,
        DHCP_OPTION_INTERFACE_MTU = 26,
        DHCP_OPTION_BROADCAST_ADDRESS = 28,
        DHCP_OPTION_REQUESTED_ADDRESS = 50,
        DHCP_OPTION_LEASE_TIME = 51,
        DHCP_OPTION_MESSAGE_TYPE = 53,
        DHCP_OPTION_DHCP_SERVER = 54,
        DHCP_OPTION_PARAMETER_REQUEST_LIST = 55,
        DHCP_OPTION_RENEWAL_TIME = 58,
        DHCP_OPTION_REBINDING_TIME = 59,
        DHCP_OPTION_END = 255
    };

    enum dhcp_message_type {
        DHCP_MT_DISCOVER = 1,
        DHCP_MT_OFFER = 2,
        DHCP_MT_REQUEST = 3,
        DHCP_MT_DECLINE = 4,
        DHCP_MT_ACK = 5,
        DHCP_MT_NAK = 6,
        DHCP_MT_RELEASE = 7,
        DHCP_MT_INFORM = 8,
        DHCP_MT_LEASEQUERY = 10,
        DHCP_MT_LEASEUNASSIGNED = 11,
        DHCP_MT_LEASEUNKNOWN = 12,
        DHCP_MT_LEASEACTIVE = 13,
        DHCP_MT_INVALID = 255
    };

    constexpr u16 min_ip_len = sizeof(struct ip);
    constexpr u16 udp_len = sizeof(struct udphdr);
    constexpr u16 dhcp_bootp_len = sizeof(struct dhcp_packet);
    constexpr u16 min_options_len = 4;
    constexpr u16 min_dhcp_len = dhcp_bootp_len + min_options_len;

    ///////////////////////////////////////////////////////////////////////////

    // Representing a DHCP packet, wraps mbuf
    class dhcp_mbuf {
    public:
        enum packet_type {
            DHCP_REQUEST = 1,
            DHCP_REPLY = 2,
        };

        dhcp_mbuf(bool attached = true, struct mbuf* m = nullptr);
        ~dhcp_mbuf();

        // mbuf memory management related functions
        void detach();
        struct mbuf* get();
        void set(struct mbuf* m);

        void compose_discover(struct ifnet* ifp);
        void compose_request(struct ifnet* ifp,
                             u32 xid,
                             boost::asio::ip::address_v4 yip,
                             boost::asio::ip::address_v4 sip);

        /* Decode packet */
        bool is_valid_dhcp();
        void decode_ip_len();
        bool decode();

        u32 get_xid();
        dhcp_message_type get_message_type() { return _message_type; }
        boost::asio::ip::address_v4 get_router_ip() { return _router_ip; }
        boost::asio::ip::address_v4 get_dhcp_server_ip() {
            return _dhcp_server_ip;
        }
        boost::asio::ip::address_v4 get_your_ip() { return _your_ip; }
        std::vector<boost::asio::ip::address> get_dns_ips() { return _dns_ips; }
        boost::asio::ip::address_v4 get_subnet_mask() { return _subnet_mask; }
        boost::asio::ip::address_v4 get_broadcast_ip() { return _broadcast_ip; }
        u16 get_interface_mtu() { return _mtu; }
        int get_lease_time_sec() { return _lease_time_sec; }
        int get_renewal_time_sec() { return _renewal_time_sec; }
        int get_rebind_time_sec() { return _rebind_time_sec; }

    private:

        // Pointers for building DHCP packet
        struct ip* pip();
        struct udphdr* pudp();
        struct dhcp_packet* pdhcp();
        u8* poptions();

        // Writes a new option to pos, returns new pos
        u8* add_option(u8* pos, u8 type, u8 len, u8* buf); // memcpy
        u8* add_option(u8* pos, u8 type, u8 len, u8 data); // memset

        // Packet assembly
        void build_udp_ip_headers(size_t dhcp_len);

        // mbuf related
        void allocate_mbuf();
        bool _attached;
        struct mbuf* _m;
        u16 _ip_len;

        // Decoded variables
        dhcp_message_type _message_type;
        boost::asio::ip::address_v4 _router_ip;
        boost::asio::ip::address_v4 _dhcp_server_ip;
        // store DNS IPs as a vector of ip::address to ease working with IPV6
        // compatible libc
        std::vector<boost::asio::ip::address> _dns_ips;
        boost::asio::ip::address_v4 _subnet_mask;
        boost::asio::ip::address_v4 _broadcast_ip;
        boost::asio::ip::address_v4 _your_ip;
        u32 _lease_time_sec;
        u32 _renewal_time_sec;
        u32 _rebind_time_sec;
        u16 _mtu;
    };

    ///////////////////////////////////////////////////////////////////////////

    //
    // DHCP socket
    //  TX for a selected interface
    //  RX is done via the hook
    //
    class dhcp_socket {
    public:
        dhcp_socket(struct ifnet* ifp): _ifp(ifp) { }
        bool dhcp_send(dhcp_mbuf& packet);
    private:
        struct ifnet* _ifp;
    };

    ///////////////////////////////////////////////////////////////////////////

    class dhcp_interface_state {
    public:
        enum state {
            DHCP_INIT,
            DHCP_DISCOVER,
            DHCP_REQUEST,
            DHCP_ACKNOWLEDGE
        };

        dhcp_interface_state(struct ifnet* ifp);
        ~dhcp_interface_state();

        void discover();
        void process_packet(struct mbuf*);
        void state_discover(dhcp_mbuf &dm);
        void state_request(dhcp_mbuf &dm);

        bool is_acknowledged() { return (_state == DHCP_ACKNOWLEDGE); }

    private:
        state _state;
        struct ifnet* _ifp;
        dhcp_socket* _sock;

        // Transaction id
        u32 _xid;
    };

    ///////////////////////////////////////////////////////////////////////////

    class dhcp_worker {
    public:
        dhcp_worker();
        ~dhcp_worker();

        // Initializing a state per interface, sends discover packets
        void init(bool wait);

        void dhcp_worker_fn();
        void queue_packet(struct mbuf* m);

    private:
        sched::thread * _dhcp_thread;

        mutex _lock;
        std::list<struct mbuf*> _rx_packets;
        std::map<struct ifnet*, dhcp_interface_state*> _universe;

        // Wait for IP
        bool _have_ip;
        sched::thread * _waiter;
    };

} // namespace dhcp

#endif // !__DHCP_HH__
