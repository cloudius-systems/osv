/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>
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
    return 0;
}

bool get_ipv6_accept_rtadv(void)
{
    return (V_ip6_accept_rtadv != 0);
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
