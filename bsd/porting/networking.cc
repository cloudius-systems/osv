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
    strncpy(ifreq.ifr_name, if_name.c_str(), IFNAMSIZ);
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
    strncpy(ifra.ifra_name, if_name.c_str(), IFNAMSIZ);
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
    mask->sin_len = sizeof(struct bsd_sockaddr_in);
    broadcast->sin_family      = AF_INET;
    broadcast->sin_len         = sizeof(struct bsd_sockaddr_in);
    broadcast->sin_addr.s_addr = (addr->sin_addr.s_addr &
                                  mask->sin_addr.s_addr) |
                                 ~mask->sin_addr.s_addr ;
    strncpy(oldaddr.ifr_name, if_name.c_str(), IFNAMSIZ);
    error = in_control(NULL, SIOCGIFADDR, (caddr_t)&oldaddr, ifp, NULL);
    if (!error) {
        in_control(NULL, SIOCDIFADDR, (caddr_t)&oldaddr, ifp, NULL);
    }
    error = in_control(NULL, SIOCAIFADDR, (caddr_t)&ifra, ifp, NULL);

out:
    if_rele(ifp);
    return (error);
}

int stop_if(std::string if_name, std::string ip_addr)
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
    strncpy(ifra.ifra_name, if_name.c_str(), IFNAMSIZ);
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

int ifup(std::string if_name)
{
    int error;
    struct bsd_ifreq ifr;

    if (if_name.empty()) {
        return (EINVAL);
    }

    strncpy(ifr.ifr_name, if_name.c_str(), IFNAMSIZ);
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
