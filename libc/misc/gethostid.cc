#include <atomic>
#include <osv/stubbing.hh>
#include <osv/debug.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <bsd/porting/netport.h>
#include <bsd/sys/net/ethernet.h>

static std::atomic<uint32_t> hostid(0);

extern "C" {

void sethostid(long id) {
    hostid.store(id, std::memory_order_relaxed);
}

long gethostid()
{
    if (hostid.load(std::memory_order_relaxed))
        return hostid.load(std::memory_order_relaxed);
    // generate hostid on first call
    int ret;
    struct ifaddrs *ifaddr = nullptr;
    ret = getifaddrs(&ifaddr);
    assert(ret==0 && ifaddr != nullptr);
    for (ifaddrs* p = ifaddr; p; p = p->ifa_next) {
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_PACKET) {
            sockaddr_ll *sll = (sockaddr_ll*) p->ifa_addr;
            auto mac = sll->sll_addr;
            for (int ii=0; ii<6; ii++) {
                // Skip loopback iface - it has MAC with all bytes zero.
                if (mac[ii] != 0x00) {
                    sethostid(ether_crc32_le(mac, 6));
                    goto done;
                }

            }
        }
    }
done:
    freeifaddrs(ifaddr);
    if (hostid.load(std::memory_order_relaxed) == 0) {
        WARN_ONCE("gethostid() failed to provide unique ID.\n");
    }
    return hostid.load(std::memory_order_relaxed);
}

} // extern "C"
