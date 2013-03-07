#include <bsd/porting/netport.h>

#include <bsd/porting/networking.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/sys/sockio.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>

void osv_start_if(const char* if_name, const char* ip_addr,
    const char* bcast_addr, int masklen)
{
    struct in_aliasreq ifra;
    struct sockaddr_in* addr = &ifra.ifra_addr;
    struct sockaddr_in* mask = &ifra.ifra_mask;
    struct ifnet* ifp;

    bzero(&ifra, sizeof(struct in_aliasreq));

    /* IF Name */
    strncpy(ifra.ifra_name, if_name, IFNAMSIZ);
    ifp = ifunit_ref(if_name);

    // todo check for null

    /* IP Address */
    inet_aton(ip_addr, &addr->sin_addr);
    addr->sin_family = AF_INET;
    addr->sin_len = sizeof(struct sockaddr_in);

    /* Mask */
    mask->sin_addr.s_addr = htonl(~((1LL << (32 - masklen)) - 1) & 0xffffffff);
    mask->sin_len = sizeof(struct sockaddr_in);

    in_control(NULL, SIOCAIFADDR, (caddr_t)&ifra, ifp, NULL);
    if_rele(ifp);
}

void osv_ifup(const char* if_name)
{
    struct ifreq ifr = {0};

    ifr.ifr_flags = IFF_UP;
    strncpy(ifr.ifr_name, if_name, IFNAMSIZ);

    ifioctl(NULL, SIOCSIFFLAGS, (caddr_t)&ifr, NULL);
}

