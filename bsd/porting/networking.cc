/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>
#include <unistd.h>
#include <osv/ioctl.h>

#include <bsd/porting/netport.h>

#include <bsd/porting/networking.hh>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#ifdef INET6
#include <bsd/sys/netinet6/in6.h>
#include <bsd/sys/netinet6/in6_var.h>
#include <bsd/sys/netinet6/nd6.h>
#include <bsd/sys/netinet6/ip6_var.h>

// FIXME: inet_pton() is from musl which uses different AF_INET6
#define LINUX_AF_INET6 10
#endif // INET6
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>

namespace osv {

void for_each_if(std::function<void (std::string)> func)
{
    struct ifnet *ifp;

    IFNET_RLOCK_NOSLEEP();
    TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
        std::string str(ifp->if_xname);
        func(str);
    }
    IFNET_RUNLOCK_NOSLEEP();
}

int if_set_mtu(std::string if_name, u16 mtu)
{
    struct bsd_ifreq ifreq;

    if (if_name.empty()) {
        return (EINVAL);
    }

    bzero(&ifreq, sizeof(struct bsd_ifreq));

    /* IF Name */
    strlcpy(ifreq.ifr_name, if_name.c_str(), IFNAMSIZ);
    auto ifp = ifunit_ref(if_name.c_str());
    if (!ifp) {
        return (ENOENT);
    }

    /* MTU */
    ifreq.ifr_mtu = mtu;
    auto error = in_control(NULL, SIOCSIFMTU, (caddr_t)&ifreq, ifp, NULL);

    if_rele(ifp);
    return (error);
}

int start_if(std::string if_name, std::string ip_addr, std::string mask_addr)
{
    return if_add_addr(if_name, ip_addr, mask_addr);
}

int stop_if(std::string if_name, std::string ip_addr)
{
    std::string mask_addr;

    return if_del_addr(if_name, ip_addr, mask_addr);
}

int if_add_ipv4_addr(std::string if_name, std::string ip_addr, std::string mask_addr)
{
    int error, success;
    struct bsd_ifreq oldaddr;
    struct in_aliasreq ifra;
    struct bsd_sockaddr_in* addr      = &ifra.ifra_addr;
    struct bsd_sockaddr_in* mask      = &ifra.ifra_mask;
    struct bsd_sockaddr_in* broadcast = &ifra.ifra_broadaddr;
    struct ifnet* ifp;

    if ((if_name.empty()) || (ip_addr.empty()) || (mask_addr.empty())) {
        return (EINVAL);
    }

    bzero(&ifra, sizeof(struct in_aliasreq));

    /* IF Name */
    strlcpy(ifra.ifra_name, if_name.c_str(), IFNAMSIZ);
    ifp = ifunit_ref(if_name.c_str());
    if (!ifp) {
        return (ENOENT);
    }

    // todo check for null

    /* IP Address */
    success = inet_aton(ip_addr.c_str(), &addr->sin_addr);
    if (!success) {
        error = EINVAL;
        goto out;
    }
    addr->sin_family = AF_INET;
    addr->sin_len = sizeof(struct bsd_sockaddr_in);

    /* Mask */
    success = inet_aton(mask_addr.c_str(), &mask->sin_addr);
    if (!success) {
        error = EINVAL;
        goto out;
    }
    mask->sin_family = AF_INET;
    mask->sin_len = sizeof(struct bsd_sockaddr_in);

    broadcast->sin_family      = AF_INET;
    broadcast->sin_len         = sizeof(struct bsd_sockaddr_in);
    broadcast->sin_addr.s_addr = (addr->sin_addr.s_addr &
                                  mask->sin_addr.s_addr) |
                                 ~mask->sin_addr.s_addr ;
    strlcpy(oldaddr.ifr_name, if_name.c_str(), IFNAMSIZ);
    error = in_control(NULL, SIOCGIFADDR, (caddr_t)&oldaddr, ifp, NULL);
    if (!error) {
        in_control(NULL, SIOCDIFADDR, (caddr_t)&oldaddr, ifp, NULL);
    }
    error = in_control(NULL, SIOCAIFADDR, (caddr_t)&ifra, ifp, NULL);

out:
    if_rele(ifp);
    return (error);
}

int if_del_ipv4_addr(std::string if_name, std::string ip_addr)
{
    int error, success;
    struct in_aliasreq ifra;
    struct bsd_sockaddr_in* addr      = &ifra.ifra_addr;
    struct ifnet* ifp;

    if ((if_name.empty()) || (ip_addr.empty())) {
        return (EINVAL);
    }

    bzero(&ifra, sizeof(struct in_aliasreq));

    /* IF Name */
    strlcpy(ifra.ifra_name, if_name.c_str(), IFNAMSIZ);
    ifp = ifunit_ref(if_name.c_str());
    if (!ifp) {
        return (ENOENT);
    }

    // todo check for null

    /* IP Address */
    success = inet_aton(ip_addr.c_str(), &addr->sin_addr);
    if (!success) {
        error = EINVAL;
        goto out;
    }
    addr->sin_family = AF_INET;
    addr->sin_len = sizeof(struct bsd_sockaddr_in);

    error = in_control(NULL, SIOCDIFADDR, (caddr_t)&ifra, ifp, NULL);

out:
    if_rele(ifp);
    return (error);
}

#ifdef INET6

int if_add_ipv6_addr(std::string if_name, std::string ip_addr, std::string netmask)
{
    int error, success;
    struct in6_ifreq oldaddr;
    struct in6_aliasreq ifra;
    struct bsd_sockaddr_in6* addr      = &ifra.ifra_addr;
    struct bsd_sockaddr_in6* mask      = &ifra.ifra_prefixmask;
    //struct bsd_sockaddr_in6* dst       = &ifra.ifra_dstaddr;
    struct ifnet* ifp;

    if (if_name.empty() || ip_addr.empty() || netmask.empty()) {
        return (EINVAL);
    }

    bzero(&ifra, sizeof(struct in6_aliasreq));
    ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
    ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

    /* IF Name */
    strncpy(ifra.ifra_name, if_name.c_str(), IFNAMSIZ-1);
    ifp = ifunit_ref(if_name.c_str());
    if (!ifp) {
        return (ENOENT);
    }

    /* IP Address */
    if ((success = inet_pton(LINUX_AF_INET6, ip_addr.c_str(), &addr->sin6_addr)) != 1) {
        bsd_log(ERR, "Failed converting IPv6 address %s\n", ip_addr.c_str());
        error = EINVAL;
        goto out;
    }
    addr->sin6_family = AF_INET6;
    addr->sin6_len = sizeof(struct bsd_sockaddr_in6);

    /* Mask */
    if (inet_pton(LINUX_AF_INET6, netmask.c_str(), &mask->sin6_addr) != 1) {
        /* Interpret it as a prefix length */
        long prefix_len = strtol(netmask.c_str(), NULL, 0);
        if (prefix_len < 0 || prefix_len > 128) {
            error = EINVAL;
            goto out;
        }
        in6_prefixlen2mask(&mask->sin6_addr, prefix_len);
    }
    mask->sin6_family = AF_INET6;
    mask->sin6_len = sizeof(struct bsd_sockaddr_in6);

    strncpy(oldaddr.ifr_name, if_name.c_str(), IFNAMSIZ-1);
    error = in6_control(NULL, SIOCGIFADDR_IN6, (caddr_t)&oldaddr, ifp, NULL);
    if (!error) {
        in6_control(NULL, SIOCDIFADDR_IN6, (caddr_t)&oldaddr, ifp, NULL);
    }
    error = in6_control(NULL, SIOCAIFADDR_IN6, (caddr_t)&ifra, ifp, NULL);

out:
    if_rele(ifp);
    return (error);
}

int if_del_ipv6_addr(std::string if_name, std::string ip_addr, std::string netmask)
{
    int error, success;
    struct in6_aliasreq ifra;
    struct bsd_sockaddr_in6* addr      = &ifra.ifra_addr;
    struct bsd_sockaddr_in6* mask      = &ifra.ifra_prefixmask;
    struct ifnet* ifp;

    if (if_name.empty() || ip_addr.empty() || netmask.empty())
        return (EINVAL);

    bzero(&ifra, sizeof(struct in6_aliasreq));

    /* IF Name */
    strncpy(ifra.ifra_name, if_name.c_str(), IFNAMSIZ-1);
    ifp = ifunit_ref(if_name.c_str());
    if (!ifp) {
        return (ENOENT);
    }

    /* IP Address */
    if ((success = inet_pton(LINUX_AF_INET6, ip_addr.c_str(), &addr->sin6_addr)) != 1) {
        bsd_log(ERR, "Failed converting IPv6 address %s\n", ip_addr.c_str());
        error = EINVAL;
        goto out;
    }
    addr->sin6_family = AF_INET6;
    addr->sin6_len = sizeof(struct bsd_sockaddr_in6);

    /* Mask */
    if (inet_pton(LINUX_AF_INET6, netmask.c_str(), &mask->sin6_addr) != 1) {
        /* Interpret it as a prefix length */
        long prefix_len = strtol(netmask.c_str(), NULL, 0);
        if (prefix_len < 0 || prefix_len > 128) {
            error = EINVAL;
            goto out;
        }
        in6_prefixlen2mask(&mask->sin6_addr, prefix_len);
    }
    mask->sin6_family = AF_INET6;
    mask->sin6_len = sizeof(struct bsd_sockaddr_in6);

    error = in6_control(NULL, SIOCDIFADDR_IN6, (caddr_t)&ifra, ifp, NULL);

out:
    if_rele(ifp);
    return (error);
}

int set_ipv6_accept_rtadv(bool enable)
{
    V_ip6_accept_rtadv = enable ? 1 : 0;

    // nd6_ifattach() only sets ND6_IFF_ACCEPT_RTADV on an interface if
    // V_ip6_accept_rtadv was already true when the interface attached. NICs
    // attach during early driver probe, before this is called at boot, so we
    // must also flip the per-interface flag on interfaces that already exist,
    // otherwise received Router Advertisements are parsed but never trigger
    // SLAAC address formation.
    struct ifnet* ifp;
    IFNET_RLOCK_NOSLEEP();
    TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
        if (ifp->if_flags & IFF_LOOPBACK)
            continue;
        struct nd_ifinfo* ndi = ND_IFINFO(ifp);
        if (!ndi)
            continue;
        if (enable)
            ndi->flags |= ND6_IFF_ACCEPT_RTADV;
        else
            ndi->flags &= ~ND6_IFF_ACCEPT_RTADV;
    }
    IFNET_RUNLOCK_NOSLEEP();
    return 0;
}

bool get_ipv6_accept_rtadv(void)
{
    return (V_ip6_accept_rtadv != 0);
}

// Does this interface already have a usable (non-tentative) global IPv6
// address, i.e. did SLAAC (or a static/DHCPv6 config) succeed?
static bool if_has_global_ipv6(struct ifnet* ifp)
{
    struct bsd_ifaddr* ifa;
    bool found = false;
    IF_ADDR_RLOCK(ifp);
    TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
        if (ifa->ifa_addr->sa_family != AF_INET6)
            continue;
        struct in6_ifaddr* ia = (struct in6_ifaddr*)ifa;
        struct in6_addr* a = &ia->ia_addr.sin6_addr;
        if (IN6_IS_ADDR_LINKLOCAL(a) || IN6_IS_ADDR_UNSPECIFIED(a))
            continue;
        if (ia->ia6_flags & IN6_IFF_NOTREADY)
            continue;   // tentative / duplicated - not yet usable
        found = true;
        break;
    }
    IF_ADDR_RUNLOCK(ifp);
    return found;
}

// Trigger and wait for IPv6 stateless address autoconfiguration (SLAAC) on the
// named interface.  The netinet6 stack already forms a global address from a
// received Router Advertisement (nd6_ra_input -> prelist_update -> in6_ifadd);
// FreeBSD relies on the userspace rtsold daemon to solicit that RA, which a
// unikernel has no room for.  So we send Router Solicitations ourselves and
// poll for the resulting global address, up to timeout_ms.  Works uniformly on
// AWS VPC (which sends RAs), QEMU SLIRP ipv6=on, and QEMU/Firecracker tap
// bridges running an RA source.  Returns 0 if a global address appeared.
int if_ipv6_autoconf(std::string if_name, int timeout_ms)
{
    if (!get_ipv6_accept_rtadv())
        return EINVAL;

    struct ifnet* ifp = ifunit_ref(if_name.c_str());
    if (!ifp)
        return ENOENT;

    int rc = ETIMEDOUT;
    const int poll_ms = 100;
    // Re-solicit periodically (RFC 4861 RTR_SOLICITATION_INTERVAL is 4s, but
    // we solicit more aggressively during the short boot window).
    const int resolicit_every = 1000 / poll_ms;   // ~1s
    int elapsed = 0, tick = 0;

    while (elapsed <= timeout_ms) {
        if (if_has_global_ipv6(ifp)) {
            rc = 0;
            break;
        }
        if (tick % resolicit_every == 0) {
            // Source the RS from our link-local address if it is ready.
            struct in6_ifaddr* ll = in6ifa_ifpforlinklocal(ifp, 0);
            nd6_rs_output(ifp, ll);
            if (ll)
                ifa_free(&ll->ia_ifa);
        }
        usleep(poll_ms * 1000);
        elapsed += poll_ms;
        tick++;
    }

    if_rele(ifp);
    return rc;
}

// Did the last Router Advertisement on this interface set the Managed (M)
// flag, i.e. does the network want us to use DHCPv6 for addresses rather than
// SLAAC? AWS VPC sets this. Returns false if the interface is unknown.
bool if_ipv6_wants_dhcp(std::string if_name)
{
    struct ifnet* ifp = ifunit_ref(if_name.c_str());
    if (!ifp)
        return false;
    struct nd_ifinfo* ndi = ND_IFINFO(ifp);
    bool managed = ndi && ndi->managed;
    if_rele(ifp);
    return managed;
}

#endif // INET6

int if_add_addr(std::string if_name, std::string ip_addr, std::string mask_addr)
{
    struct in_addr v4;
    if (inet_pton(AF_INET, ip_addr.c_str(), &v4)) {
        return if_add_ipv4_addr(if_name, ip_addr, mask_addr);
    }
#ifdef INET6
    else {
        return if_add_ipv6_addr(if_name, ip_addr, mask_addr);
    }
#endif
    return EINVAL;
}

int if_del_addr(std::string if_name, std::string ip_addr, std::string mask_addr)
{
    struct in_addr v4;
    if (inet_pton(AF_INET, ip_addr.c_str(), &v4)) {
        return if_del_ipv4_addr(if_name, ip_addr);
    }
#ifdef INET6
    else {
        return if_del_ipv6_addr(if_name, ip_addr, mask_addr);
    }
#endif
    return EINVAL;
}

int ifup(std::string if_name)
{
    int error;
    struct bsd_ifreq ifr;

    if (if_name.empty()) {
        return (EINVAL);
    }

    strlcpy(ifr.ifr_name, if_name.c_str(), IFNAMSIZ);
    error = ifioctl(NULL, SIOCGIFFLAGS, (caddr_t)&ifr, NULL);
    if (error) {
        return (error);
    }

    // FIXME: SIOCGIFFLAGS returns IFF_UP for some reason for virtio-net0
    // on the first call to osv_ifup(), this should be investigated

    ifr.ifr_flags |= IFF_UP;
    error = ifioctl(NULL, SIOCSIFFLAGS, (caddr_t)&ifr, NULL);
    return (error);
}

std::string if_ip(std::string if_name) {
    struct ifnet *ifp;
    ifp = ifunit_ref(if_name.c_str());
    if (!ifp) {
        return ("");
    }

    struct bsd_ifreq addr;
    int error=in_control(NULL, SIOCGIFADDR, (caddr_t)&addr, ifp, NULL);
    if_rele(ifp);

    if (error) {
       return ("");
    }
    return inet_ntoa(((bsd_sockaddr_in*)&(addr.ifr_addr))->sin_addr);
}


}
