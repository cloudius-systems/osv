/*-
 * Copyright (c) 1988, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdio.h>
#include <unistd.h>
#include <osv/debug.h>
#include <osv/ioctl.h>


#define _KERNEL

#include <bsd/porting/netport.h>
#include <bsd/porting/networking.h>
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
#include <bsd/machine/in_cksum.h>

#include <string>
using std::string ;

#if !defined(lengthof)
#define lengthof(a) (sizeof (a) / sizeof(a[0]))
#define lastof(a) (lengthof(a) - 1)
#endif

const char *prog_name = "ifconfig" ;

static char hexlist[] = "0123456789abcdef";

struct flag_name_struct {
    uint32_t        mask ;
    const char      *name ;    
} ;

class interface_class
{
public:
    //! Class constructor
    interface_class(const char *iface_name) : name(iface_name)
    {
        socreate(AF_INET, &sock, SOCK_DGRAM, 0, NULL, NULL);
    }

    //! Class destructor
    ~interface_class()
    {
        soclose(sock) ;
    }

    //! Set interface name
    void    set_name(const char *iface_name) { name = iface_name ; }

    //! Get interface name
    const string get_name() const { return name ; }

    //! Get ip address
    string  get_addr() const        { return do_get_addr(SIOCGIFADDR) ; }

    //! Get ip mask
    string  get_mask() const        { return do_get_addr(SIOCGIFNETMASK) ; }

    //! Get ip broadcast address
    string  get_broadcast() const   { return do_get_addr(SIOCGIFBRDADDR) ; }
    
    //! Get interface flags
    string  get_flags() const
    {
        uint32_t flags ;
        ifreq ifr;

        if (do_get_ifr(SIOCGIFFLAGS, &ifr)  == 0) {
            flags = ((uint32_t)ifr.ifr_flagshigh << 16) | ifr.ifr_flags ;
            return flags2str(flags) ;
        }
        else
            return "" ;
    }

    //! Get mtu
    string  get_mtu() const
    {
        ifreq ifr ;

        if (do_get_ifr(SIOCGIFMTU, &ifr)  == 0)
            return std::to_string(ifr.ifr_mtu);
        else
            return "" ;
    }
    
private:
    //! Get interface address/mask/broadcast
    string  do_get_addr(u_long cmd) const
    {
        ifreq ifr;
        char buf[64] ;

        if (do_get_ifr(cmd, &ifr) == 0)
            inet_ntoa_r(((bsd_sockaddr_in *)&(ifr.ifr_addr))->sin_addr, buf) ;
        else
            buf[0] = '\0' ;
        return buf ;
    }

    //! A wrapper around ifr requests.
    int do_get_ifr(u_long cmd, ifreq *ifrp) const
    {
        bzero(ifrp, sizeof(*ifrp)) ;
        strncpy(ifrp->ifr_name, name.c_str(), IFNAMSIZ);
        return ifioctl(sock, cmd, (caddr_t)ifrp, NULL) ;
    }

    static string flags2str(uint32_t flag) ;

    struct socket   *sock ;
    string          name ;
} ;


string interface_class::flags2str(uint32_t flags)
{
    char buf[64] ;
    string s ;
    bool first_time = true ;
    snprintf(buf, sizeof(buf), "%o<", flags);

    static flag_name_struct flag_names[] = {
        { IFF_UP,             "UP"          },
        { IFF_BROADCAST,      "BROADCAST"   },
        { IFF_DEBUG,          "DEBUG"       },
        { IFF_LOOPBACK,       "LOOPBACK"    },
        { IFF_POINTOPOINT,    "POINTOPOINT" },
        { IFF_SMART,          "SMART"       },
        { IFF_DRV_RUNNING,    "RUNNING"     },
        { IFF_NOARP,          "NOARP"       },
        { IFF_PROMISC,        "PROMISC"     },
        { IFF_ALLMULTI,       "ALLMULTI"    },
        { IFF_DRV_OACTIVE,    "ACTIVE"      },
        { IFF_SIMPLEX,        "SIMPLEX"     },
        { IFF_LINK0,          "LINK0"       },
        { IFF_LINK1,          "LINK1"       },
        { IFF_LINK2,          "LINK2"       },
        { IFF_MULTICAST,      "MULTICAST"   },
        { IFF_PPROMISC,       "PPROMISC"    },
        { IFF_MONITOR,        "MONITOR"     },
        { IFF_STATICARP,      "STATICARP"   },
    } ;
    
    s = buf ;
    for (unsigned i = 0; i <= lastof(flag_names); i++)
    {
        if ((flags & flag_names[i].mask)) {
            if (!first_time)
                s += ',' ;
            else
                first_time = false ;
            s += flag_names[i].name ;
        }
    }
    s += ">" ;
    return s ;
}



char *
link_ntoa(const struct bsd_sockaddr_dl *sdl, char obuf[64])
{
	char *out = obuf;
	int i;
	u_char *in = (u_char *)LLADDR(sdl);
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

static string bytes2str(long bytes)
{
    char buf[100] ;

    if (bytes > 1000000000000)
        snprintf(buf, sizeof(buf), "(%.1fTiB)", (double)bytes / 1000000000000.) ;
    else if (bytes > 1000000000)
        snprintf(buf, sizeof(buf), "(%.1f GiB)", (double)bytes / 1000000000.) ;
    else if (bytes > 1000000)
        snprintf(buf, sizeof(buf), "(%.1f MiB)", (double)bytes / 1000000.) ;
    else if (bytes > 1000)
        snprintf(buf, sizeof(buf), "(%.1f KiB)", (double)bytes / 1000.) ;
    else
        buf[0] = '\0' ;
    return buf ;
}

int main(int argc, const char **argv)
{
    struct ifnet *ifp ;
    char phys_addr[64]/*, addr[16], broadcast[16], mask[16] */;
    
    printf("%s argc=%d argv[0]=%s\n", prog_name, argc, argv[0]) ;

    for (u_short i = 0; i <= V_if_index; i++)
    {
        ifp = ifnet_byindex_ref(i) ;
        if (ifp != NULL) {
            interface_class interface(if_name(ifp)) ;
            if (ifp->if_addr && ifp->if_addrlen && ifp->if_type == IFT_ETHER)
            {
                link_ntoa((struct bsd_sockaddr_dl *)ifp->if_addr->ifa_addr,
                          phys_addr) ;
            }
            else
                phys_addr[0] = '\0' ;
            printf("\n") ;
            printf("%s: flags=%s  mtu %s\n",
                   interface.get_name().c_str(),
                   interface.get_flags().c_str(),
                   interface.get_mtu().c_str()) ;
            printf("        inet  %s  netmask %s  broadcast %s\n",
                   interface.get_addr().c_str(), 
                   interface.get_mask().c_str(),
                   interface.get_broadcast().c_str()) ;
            if (ifp->if_type == IFT_ETHER)
                printf("        ether %s\n", phys_addr) ;
            printf("        RX packets %ld  bytes %ld %s\n",
                   ifp->if_ipackets, ifp->if_ibytes,
                   bytes2str(ifp->if_ibytes).c_str()) ;
            printf("        TX packets %ld  bytes %ld %s\n",
                   ifp->if_opackets, ifp->if_obytes,
                   bytes2str(ifp->if_obytes).c_str()) ;
            printf("        dropped    %ld  collisions %ld\n",
                   ifp->if_iqdrops, ifp->if_collisions) ;
            if_rele(ifp) ;
        }
    }

    return (0);
}
