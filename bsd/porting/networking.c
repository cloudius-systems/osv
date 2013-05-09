#include <osv/ioctl.h>

#include <bsd/porting/netport.h>

#include <bsd/porting/networking.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>

void osv_start_if(const char* if_name, const char* ip_addr, const char* mask_addr)
{
    struct in_aliasreq ifra;
    struct sockaddr_in* addr      = &ifra.ifra_addr;
    struct sockaddr_in* mask      = &ifra.ifra_mask;
    struct sockaddr_in* broadcast = &ifra.ifra_broadaddr;
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
    inet_aton(mask_addr, &mask->sin_addr);
    mask->sin_len = sizeof(struct sockaddr_in);
    broadcast->sin_family      = AF_INET;
    broadcast->sin_len         = sizeof(struct sockaddr_in);
    broadcast->sin_addr.s_addr = (addr->sin_addr.s_addr &
                                  mask->sin_addr.s_addr) |
                                 ~mask->sin_addr.s_addr ;
    in_control(NULL, SIOCAIFADDR, (caddr_t)&ifra, ifp, NULL);
    if_rele(ifp);
}

void osv_ifup(const char* if_name)
{
    int error;
    struct ifreq ifr = {0};

    strncpy(ifr.ifr_name, if_name, IFNAMSIZ);

    error = ifioctl(NULL, SIOCGIFFLAGS, (caddr_t)&ifr, NULL);
    if (error) {
        return;
    }

    if (ifr.ifr_flags & IFF_UP) {
        return;
    }

    ifr.ifr_flags |= IFF_UP;
    ifioctl(NULL, SIOCSIFFLAGS, (caddr_t)&ifr, NULL);
}

