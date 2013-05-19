#include <errno.h>
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

int osv_start_if(const char* if_name, const char* ip_addr, const char* mask_addr)
{
    int error, success;
    struct in_aliasreq ifra;
    struct bsd_sockaddr_in* addr      = &ifra.ifra_addr;
    struct bsd_sockaddr_in* mask      = &ifra.ifra_mask;
    struct bsd_sockaddr_in* broadcast = &ifra.ifra_broadaddr;
    struct ifnet* ifp;

    if ((if_name == NULL) || (ip_addr == NULL) || (mask_addr == NULL)) {
        return (EINVAL);
    }

    bzero(&ifra, sizeof(struct in_aliasreq));

    /* IF Name */
    strncpy(ifra.ifra_name, if_name, IFNAMSIZ);
    ifp = ifunit_ref(if_name);
    if (!ifp) {
        return (ENOENT);
    }

    // todo check for null

    /* IP Address */
    success = inet_aton(ip_addr, &addr->sin_addr);
    if (!success) {
        error = EINVAL;
        goto out;
    }
    addr->sin_family = AF_INET;
    addr->sin_len = sizeof(struct bsd_sockaddr_in);

    /* Mask */
    success = inet_aton(mask_addr, &mask->sin_addr);
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
    error = in_control(NULL, SIOCAIFADDR, (caddr_t)&ifra, ifp, NULL);

out:
    if_rele(ifp);
    return (error);
}

int osv_ifup(const char* if_name)
{
    int error;
    struct ifreq ifr = {0};

    if (if_name == NULL) {
        return (EINVAL);
    }

    strncpy(ifr.ifr_name, if_name, IFNAMSIZ);
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

