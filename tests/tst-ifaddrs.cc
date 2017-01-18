/*
 * Copyright (C) 2016 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// To compile on Linux, use: c++ -g -O2 -pthread -std=c++11 tests/tst-ifaddrs.cc

#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
//#include <netpacket/packet.h>
#include <string.h>

#include <iostream>
#include <thread>


#include <linux/if_packet.h> // for sockaddr_ll

static int tests = 0, fails = 0;

template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected " << expecteds << "(" << expected << "), saw "
                << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
template<typename T>
bool do_expectge(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual < expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected >=" << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expectge(actual, expected) do_expectge(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expect_errno(call, experrno) ( \
        do_expect((long)(call), (long)-1, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )
#define expect_success(var, call) \
        errno = 0; \
        var = call; \
        do_expectge(var, 0, #call, "0", __FILE__, __LINE__); \
        do_expect(errno, 0, #call " errno",  "0", __FILE__, __LINE__);

int main()
{
    // Start by testing if_nameindex() because Musl's get_ifaddrs() actually
    // uses that, and the second won't work if the first doesn't.
    struct if_nameindex *ni = if_nameindex();
    expect(ni != nullptr, true);
    int count = 0;
    if (!ni) goto noni;
    for (auto p = ni ; p->if_index ; p++) {
        std::cout << p->if_index << " " << p->if_name << "\n";
        count++;
    }
    if_freenameindex(ni);
    // We should usually have at least loopback and ethernet devices, so
    // let's check the count
    expectge(count, 2);
noni:

    // Test getifaddrs()
    struct ifaddrs *ifaddr = 0;
    expect(getifaddrs(&ifaddr), 0);
    expect(ifaddr != nullptr, true);

    // On OSv, we expect to have an interface called "lo0", and on Linux,
    // "lo". Each should have the IP address 127.0.0.1 and the MAC address
    // 0.
    bool found_lo_inet = false, found_lo_ll = false;
    for (ifaddrs* p = ifaddr; p; p = p->ifa_next) {
        printf("%s\n", p->ifa_name);
        if (!strcmp(p->ifa_name, "lo0") || !strcmp(p->ifa_name, "lo")) {
            // lo is supposed to be up and running, and have the LOOPBACK
            // flag.
            expect(p->ifa_flags & IFF_UP, (unsigned)IFF_UP);
            expect(p->ifa_flags & IFF_RUNNING, (unsigned)IFF_RUNNING);
            expect(p->ifa_flags & IFF_LOOPBACK, (unsigned)IFF_LOOPBACK);
            // getifaddrs() is expected to return the loopback twice:
            // once as AF_INET with an IPv4 address, and once as
            // AF_PACKET with a MAC address. The second one can be used to
            // retrieve the MAC addresses of the interfaces.
            // In Linux, we can also get AF_INET6, but we don't have that
            // on OSv currently.
            expect(p->ifa_addr == NULL, false);
            if (p->ifa_addr) {
                expect(p->ifa_addr->sa_family == AF_INET || p->ifa_addr->sa_family == AF_PACKET || p->ifa_addr->sa_family == AF_INET6, true);
                if (p->ifa_addr->sa_family == AF_INET) {
                    expect(found_lo_inet, false);
                    found_lo_inet = true;
                    sockaddr_in *a = (sockaddr_in*) p->ifa_addr;
                    expect(a->sin_addr.s_addr, ntohl(INADDR_LOOPBACK));
                } else if (p->ifa_addr->sa_family == AF_PACKET) {
                    expect(found_lo_ll, false);
                    found_lo_ll = true;
                    sockaddr_ll *sll = (sockaddr_ll*) p->ifa_addr;
                    auto mac = sll->sll_addr;
                    expect (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 && mac[3] == 0 && mac[4] == 0 && mac[5] == 0, true);
                }
            }
        }
    }
    expect(found_lo_inet && found_lo_ll, true);

    // Print out the interfaces we have. This is not really a test, but can
    // be useful for manual testing.
    ifaddrs *ifa;
    int s, n;
    char host[NI_MAXHOST];

    for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
        if (ifa->ifa_addr == NULL)
            continue;

        auto family = ifa->ifa_addr->sa_family;

        /* Display interface name and family (including symbolic
           form of the latter for the common families) */

        printf("%-8s %s (%d)\n",
               ifa->ifa_name,
               (family == AF_PACKET) ? "AF_PACKET" :
               (family == AF_INET) ? "AF_INET" :
               (family == AF_INET6) ? "AF_INET6" : "???",
               family);

        if (family == AF_INET || family == AF_INET6) {
            /* For an AF_INET* interface address, display the IP address */
            s = getnameinfo(ifa->ifa_addr,
                    (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                          sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST,
                    NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }

            printf("\t\taddress: <%s>\n", host);
            sockaddr_in *nm = (sockaddr_in*) ifa->ifa_netmask;
            printf("\t\tnetmask: %s\n", inet_ntoa(nm->sin_addr));
        } else if (family == AF_PACKET) {
            // For an AF_PACKET interface, display the MAC address.
            // TODO: In Linux, one can also do, if ifa->ifa_data != nullptr,
            //  struct rtnl_link_stats *stats = (rtnl_link_stats*) ifa->ifa_data;
            // and then print stats->tx_packets, stats->tx_bytes, etc.
            // but we don't support this on OSv yet.
            sockaddr_ll *sll = (struct sockaddr_ll*)ifa->ifa_addr;
            unsigned char *ph = (unsigned char*) sll->sll_addr;
            printf("\t\tMAC address: %02x:%02x:%02x:%02x:%02x:%02x\n", ph[0], ph[1], ph[2], ph[3], ph[4], ph[5]);
        }
    }

    freeifaddrs(ifaddr);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
