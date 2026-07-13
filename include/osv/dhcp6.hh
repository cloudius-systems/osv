/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __DHCP6_HH__
#define __DHCP6_HH__

#include <osv/kernel_config_networking_dhcp6.h>
#if CONF_networking_dhcp6

#include <vector>
#include <string>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/netinet/in.h>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/debug.h>

extern "C" {
// Start the DHCPv6 client and (optionally) wait until an address is bound.
// Only meaningful when the on-link Router Advertisement set the Managed (M)
// flag; on SLAAC-only (A-flag) networks this is not used.
void dhcp6_start(bool wait);
}

namespace dhcp6 {

    #define dhcp6_tag "dhcp6"
    #define dhcp6_d(...)   tprintf_d(dhcp6_tag, __VA_ARGS__)
    #define dhcp6_i(...)   tprintf_i(dhcp6_tag, __VA_ARGS__)
    #define dhcp6_w(...)   tprintf_w(dhcp6_tag, __VA_ARGS__)
    #define dhcp6_e(...)   tprintf_e(dhcp6_tag, __VA_ARGS__)

    // RFC 8415 well-known ports and multicast group.
    constexpr u_short client_port = 546;
    constexpr u_short server_port = 547;
    // ff02::1:2 - All_DHCP_Relay_Agents_and_Servers link-local multicast.

    // RFC 8415 message types.
    enum msg_type {
        DH6_SOLICIT     = 1,
        DH6_ADVERTISE   = 2,
        DH6_REQUEST     = 3,
        DH6_CONFIRM     = 4,
        DH6_RENEW       = 5,
        DH6_REBIND      = 6,
        DH6_REPLY       = 7,
        DH6_RELEASE     = 8,
        DH6_DECLINE     = 9,
        DH6_INFORMATION_REQUEST = 11,
    };

    // RFC 8415 option codes we handle.
    enum opt_code {
        D6O_CLIENTID    = 1,
        D6O_SERVERID    = 2,
        D6O_IA_NA       = 3,
        D6O_IAADDR      = 5,
        D6O_ORO         = 6,   // option request
        D6O_ELAPSED_TIME = 8,
        D6O_STATUS_CODE = 13,
        D6O_RAPID_COMMIT = 14, // RFC 8415 21.14 - two-message (SOLICIT/REPLY) exchange
        D6O_DNS_SERVERS = 23,
    };

    enum status_code {
        DH6_STATUS_SUCCESS = 0,
        DH6_STATUS_NOADDRSAVAIL = 2,
    };

    // Client state machine (RFC 8415 stateful, minimal: SOLICIT -> REQUEST).
    enum state {
        DH6_INIT,
        DH6_SOLICITING,
        DH6_REQUESTING,
        DH6_BOUND,
    };

    // Per-interface DHCPv6 client.
    class dhcp6_interface_state {
    public:
        dhcp6_interface_state(struct ifnet* ifp);

        // Send a SOLICIT (start) or REQUEST (after ADVERTISE) message.
        void solicit();
        void request();
        // Feed a received DHCPv6 UDP payload to the state machine.
        void process_packet(struct mbuf* m);

        bool is_bound() const { return _state == DH6_BOUND; }
        struct ifnet* ifp() const { return _ifp; }

    private:
        // Build a DHCPv6 message of the given type into a fresh mbuf and send
        // it to ff02::1:2. Includes our Client-ID, optional Server-ID, an
        // IA_NA and an ORO for DNS.
        void send_message(msg_type type, bool include_server_id,
                          bool include_iaaddr);
        // Parse a REPLY/ADVERTISE; extract the assigned address, DNS, server-id.
        bool parse(struct mbuf* m, msg_type& out_type);
        // Install the bound address + DNS on the interface.
        void bind_address();

        struct ifnet* _ifp;
        state _state;
        u32 _xid;                       // 24-bit transaction id
        std::vector<u8> _duid;          // our client DUID (DUID-LLT)
        std::vector<u8> _server_duid;   // server's DUID from ADVERTISE
        u32 _iaid;                      // IA identifier

        // Learned from the server.
        struct in6_addr _addr;
        u8 _prefixlen;
        u32 _pref_lifetime;
        u32 _valid_lifetime;
        std::vector<std::string> _dns;   // DNS servers in presentation form
        bool _have_addr;
    };

    class dhcp6_worker {
    public:
        dhcp6_worker();
        ~dhcp6_worker();

        void init();
        void start(bool wait);
        void queue_packet(struct mbuf* m);
        void dhcp6_worker_fn();

    private:
        sched::thread* _thread;
        mutex _lock;
        std::list<struct mbuf*> _rx_packets;
        std::map<struct ifnet*, dhcp6_interface_state*> _universe;
        bool _bound;
        sched::thread* _waiter;
    };

} // namespace dhcp6

#endif // CONF_networking_dhcp6
#endif // __DHCP6_HH__
