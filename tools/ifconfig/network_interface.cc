/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "network_interface.hh"
#include <stdio.h>
#include <unistd.h>
#include <osv/debug.h>
#include <osv/ioctl.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/networking.hh>
#include <bsd/porting/route.h>
#include <bsd/porting/callout.h>
#undef NZERO
#include <bsd/sys/sys/param.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_arp.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/ip_icmp.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <machine/in_cksum.h>
#include <string>

#if !defined(lengthof)
#define lengthof(a) (sizeof (a) / sizeof(a[0]))
#define lastof(a) (lengthof(a) - 1)
#endif

namespace osv {
namespace network {
using namespace std;

static char hexlist[] = "0123456789abcdef";
struct flag_name_struct {
    uint32_t mask;
    const char *name;
};

static flag_name_struct flag_names[] = {
    { IFF_UP, "UP" },
    { IFF_BROADCAST, "BROADCAST" },
    { IFF_DEBUG, "DEBUG" },
    { IFF_LOOPBACK, "LOOPBACK" },
    { IFF_POINTOPOINT, "POINTOPOINT" },
    { IFF_SMART, "SMART" },
    { IFF_DRV_RUNNING, "RUNNING" },
    { IFF_NOARP, "NOARP" },
    { IFF_PROMISC, "PROMISC" },
    { IFF_ALLMULTI, "ALLMULTI" },
    { IFF_DRV_OACTIVE, "ACTIVE" },
    { IFF_SIMPLEX, "SIMPLEX" },
    { IFF_LINK0, "LINK0" },
    { IFF_LINK1, "LINK1" },
    { IFF_LINK2, "LINK2" },
    { IFF_MULTICAST, "MULTICAST" },
    { IFF_PPROMISC, "PPROMISC" },
    { IFF_MONITOR, "MONITOR" },
    { IFF_STATICARP, "STATICARP" },
};

/**
 * A wrapper around ifr requests.
 */
static int do_ifr(struct socket *sock, const std::string& name, u_long cmd,
                  bsd_ifreq *ifrp)
{
    bzero(ifrp, sizeof(*ifrp));
    strncpy(ifrp->ifr_name, name.c_str(), IFNAMSIZ);
    return ifioctl(sock, cmd, (caddr_t) ifrp, NULL);
}

static std::string do_addr(struct socket *sock, const std::string& name,
                           u_long cmd)
{
    bsd_ifreq ifr;
    char buf[64];

    if (do_ifr(sock, name, cmd, &ifr) == 0)
        inet_ntoa_r(((bsd_sockaddr_in *) &(ifr.ifr_addr))->sin_addr, buf,
                    sizeof buf);
    else
        buf[0] = '\0';
    return buf;
}

std::string interface::bytes2str(long bytes)
{
    char buf[100];

    if (bytes > 1000000000000)
        snprintf(buf, sizeof(buf), "(%.1fTiB)",
                 (double) bytes / 1000000000000.);
    else if (bytes > 1000000000)
        snprintf(buf, sizeof(buf), "(%.1f GiB)", (double) bytes / 1000000000.);
    else if (bytes > 1000000)
        snprintf(buf, sizeof(buf), "(%.1f MiB)", (double) bytes / 1000000.);
    else if (bytes > 1000)
        snprintf(buf, sizeof(buf), "(%.1f KiB)", (double) bytes / 1000.);
    else
        buf[0] = '\0';
    return buf;
}

std::string flags2str(uint32_t flags)
{
    char buf[64];
    std::string s;
    bool first_time = true;
    snprintf(buf, sizeof(buf), "%o<", flags);

    s = buf;
    for (unsigned i = 0; i <= lastof(flag_names); i++)
    {
        if ((flags & flag_names[i].mask)) {
            if (!first_time)
                s += ',';
            else
                first_time = false;
            s += flag_names[i].name;
        }
    }
    s += ">";
    return s;
}

interface::interface(const std::string& iface_name)
    : name(iface_name)
{
    socreate(AF_INET, &sock, SOCK_DGRAM, 0, NULL, NULL);
    addr = do_addr(sock, name, SIOCGIFADDR);
    //! Get ip mask
    mask = do_addr(sock, name, SIOCGIFNETMASK);
    //! Get ip broadcast address
    broadcast = do_addr(sock, name, SIOCGIFBRDADDR);
    uint32_t uflags;
    bsd_ifreq ifr;

    if (do_ifr(sock, name, SIOCGIFFLAGS, &ifr) == 0) {
        uflags = ((uint32_t) ifr.ifr_flagshigh << 16) | ifr.ifr_flags;
        flags = flags2str(uflags);
    }

    if (do_ifr(sock, name, SIOCGIFMTU, &ifr) == 0) {
        mtu = std::to_string(ifr.ifr_mtu);
    }
    soclose(sock);
}

char *
link_ntoa(const struct bsd_sockaddr_dl *sdl, char obuf[64])
{
    char *out = obuf;
    int i;
    u_char *in = (u_char *) LLADDR(sdl);
    u_char *inlim = in + sdl->sdl_alen;
    int firsttime = 1;

    while (in < inlim) {
        if (firsttime)
            firsttime = 0;
        else
            *out++ = ':';
        i = *in++;
        if (i > 0xf) {
            out[1] = hexlist[i & 0xf];
            i >>= 4;
            out[0] = hexlist[i];
            out += 2;
        } else
            *out++ = hexlist[i];
    }
    *out = 0;
    return (obuf);
}

unsigned short int number_of_interfaces()
{
    return V_if_index;
}

struct ifnet* get_interface_by_index(unsigned int i)
{
    return ifnet_byindex_ref(i);
}

bool set_interface_info(struct ifnet* ifp, if_data& data)
{
    if (ifp == nullptr) {
        return false;
    }
    char phys_addr[64];
    if (ifp->if_addr && ifp->if_addrlen && ifp->if_type == IFT_ETHER)
    {
        link_ntoa((struct bsd_sockaddr_dl *) ifp->if_addr->ifa_addr,
                  phys_addr);
    }
    else {
        phys_addr[0] = '\0';
    }
    if (!ifp->if_getinfo) {
        return false;
    }
    ifp->if_getinfo(ifp, (::if_data*) &data);
    return true;
}

std::string get_interface_name(struct ifnet* ifp)
{
    if (ifp == nullptr) {
        return "";
    }
    string name = ifp->if_xname;
    return name;
}
struct ifnet* get_interface_by_name(const std::string& name)
{
    struct ifnet* ifp;
    for (unsigned int i = 0; i < number_of_interfaces(); i++) {
        ifp = get_interface_by_index(i);
        if (get_interface_name(ifp) == name) {
            return ifp;
        }
    }
    return nullptr;
}

}

}
