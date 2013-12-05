/*-
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <bsd/sys/sys/param.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/file.h>
#include <malloc.h>
#include <bsd/sys/sys/sbuf.h>
#include <stdint.h>
#include <bsd/sys/sys/socket.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_types.h>

#include <bsd/sys/compat/linux/linux_socket.h>
#include <bsd/sys/compat/linux/linux.h>

#include <osv/file.h>
#include <osv/socket.hh>
#include <osv/ioctl.h>
#include <sys/ioctl.h>

/*
 * Implement the SIOCGIFCONF ioctl
 */

static int
linux_ifconf(struct bsd_ifconf *ifc_p)
{
    struct l_ifreq ifr;
    struct ifnet *ifp;
    struct bsd_ifaddr *ifa;
    struct sbuf *sb;
    int full = 0, valid_len, max_len;

    max_len = MAXPHYS - 1;

    /* handle the 'request buffer size' case */
    if ((void *)ifc_p->ifc_buf == NULL) {
        ifc_p->ifc_len = 0;
        IFNET_RLOCK();
        TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
            TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
                struct bsd_sockaddr *sa = ifa->ifa_addr;
                if (sa->sa_family == AF_INET)
                    ifc_p->ifc_len += sizeof(ifr);
            }
        }
        IFNET_RUNLOCK();
        return (0);
    }

    if (ifc_p->ifc_len <= 0) {
        return (EINVAL);
    }

    again:
    /* Keep track of eth interfaces */
    if (ifc_p->ifc_len <= max_len) {
        max_len = ifc_p->ifc_len;
        full = 1;
    }
    sb = sbuf_new(NULL, NULL, max_len + 1, SBUF_FIXEDLEN);
    max_len = 0;
    valid_len = 0;

    /* Return all AF_INET addresses of all interfaces */
    IFNET_RLOCK();
    TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
        int addrs = 0;

        bzero(&ifr, sizeof(ifr));
        strlcpy(ifr.ifr_name, ifp->if_xname, IFNAMSIZ);

        /* Walk the address list */
        TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
            struct bsd_sockaddr *sa = ifa->ifa_addr;

            if (sa->sa_family == AF_INET) {
                ifr.ifr_addr.sa_family = LINUX_AF_INET;
                memcpy(ifr.ifr_addr.sa_data, sa->sa_data,
                        sizeof(ifr.ifr_addr.sa_data));
                sbuf_bcat(sb, &ifr, sizeof(ifr));
                max_len += sizeof(ifr);
                addrs++;
            }

            if (sbuf_error(sb) == 0)
                valid_len = sbuf_len(sb);
        }
        if (addrs == 0) {
            bzero((caddr_t)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
            sbuf_bcat(sb, &ifr, sizeof(ifr));
            max_len += sizeof(ifr);

            if (sbuf_error(sb) == 0)
                valid_len = sbuf_len(sb);
        }
    }
    IFNET_RUNLOCK();

    if (valid_len != max_len && !full) {
        sbuf_delete(sb);
        goto again;
    }

    ifc_p->ifc_len = valid_len;
    sbuf_finish(sb);
    memcpy(ifc_p->ifc_buf, sbuf_data(sb), ifc_p->ifc_len);
    sbuf_delete(sb);

    return (0);
}

static void
linux_gifflags(struct ifnet *ifp, struct l_ifreq *ifr)
{
    l_short flags;

    flags = (ifp->if_flags | ifp->if_drv_flags);
    /* These flags have no Linux equivalent
     *
     *  Notes:
     *       - We do show IFF_SMART|IFF_DRV_OACTIVE|IFF_SIMPLEX
     *       - IFF_LINK0 has a value of 0x1000 which conflics with the Linux
     *         IFF_MULTICAST value.
     */
    flags &= ~(IFF_LINK0|IFF_LINK1|IFF_LINK2);
    /* Linux' multicast flag is in a different bit */
    if (flags & IFF_MULTICAST) {
        flags &= ~IFF_MULTICAST;
        flags |= 0x1000;
    }
    ifr->ifr_flags = flags ;
}

#define ARPHRD_ETHER	1
#define ARPHRD_LOOPBACK	772

static int
linux_gifhwaddr(struct ifnet *ifp, struct l_ifreq *ifr)
{
    struct bsd_ifaddr *ifa;
    struct bsd_sockaddr_dl *sdl;
    struct l_sockaddr *lsa_p = &ifr->ifr_hwaddr ;

    if (ifp->if_type == IFT_LOOP) {
        bzero(lsa_p, sizeof(*lsa_p));
        lsa_p->sa_family = ARPHRD_LOOPBACK;
        return 0;
    }

    if (ifp->if_type != IFT_ETHER)
        return (ENOENT);

    TAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
        sdl = (struct bsd_sockaddr_dl*)ifa->ifa_addr;
        if (sdl != NULL && (sdl->sdl_family == AF_LINK) &&
                (sdl->sdl_type == IFT_ETHER)) {

            bzero(lsa_p, sizeof(*lsa_p));
            lsa_p->sa_family = ARPHRD_ETHER;
            bcopy(LLADDR(sdl), lsa_p->sa_data, LINUX_IFHWADDRLEN);
            return 0;
        }
    }
    return (ENOENT);
}


/*
 * Fix the interface address field in bsd_ifreq. The bsd stack expects a
 * length/family byte members, while linux and everyone else use a short family
 * field.
 */
static inline void
bsd_to_linux_ifreq(struct bsd_ifreq *ifr_p)
{
    u_short family = ifr_p->ifr_addr.sa_family ;
    *(u_short *)&ifr_p->ifr_addr = family;
}

// Convert Linux bsd_sockaddr to bsd
static inline void
linux_to_bsd_ifreq(struct bsd_ifreq *ifr_p)
{
    u_short family = *(u_short *)&(ifr_p->ifr_addr) ;

    ifr_p->ifr_addr.sa_family = family ;
    ifr_p->ifr_addr.sa_len    = 16 ;
}

/*
 * Socket related ioctls
 */

extern "C" int linux_ioctl_socket(socket_file *fp, u_long cmd, void *data);

int
linux_ioctl_socket(socket_file *fp, u_long cmd, void *data)
{
    struct ifnet *ifp = NULL;
    int error = 0, type = file_type(fp);
    if (type != DTYPE_SOCKET) {
        return (ENOIOCTL);
    }
    // RUN COMMAND

    switch (cmd) {
    case SIOCSIFADDR:
    case SIOCSIFNETMASK:
    case SIOCSIFDSTADDR: 
    case SIOCSIFBRDADDR: 
        if ((ifp = ifunit_ref((char *)data)) == NULL)
            return (EINVAL);
        linux_to_bsd_ifreq((struct bsd_ifreq *)data) ;
        error = fp->bsd_ioctl(cmd, data);
        break ;

    case SIOCGIFMTU:
    case SIOCSIFMTU:
    case SIOCGIFINDEX:
        if ((ifp = ifunit_ref((char *)data)) == NULL)
            return (EINVAL);
        error = fp->bsd_ioctl(cmd, data);
        break;

    case SIOCGIFADDR:
    case SIOCGIFDSTADDR:
    case SIOCGIFBRDADDR:
    case SIOCGIFNETMASK:
        if ((ifp = ifunit_ref((char *)data)) == NULL)
            return (EINVAL);
        error = fp->bsd_ioctl(cmd, data);
        bsd_to_linux_ifreq((struct bsd_ifreq *)data);
        break;

    case SIOCGIFCONF:
        error = linux_ifconf((struct bsd_ifconf *)data);
        break;

    case SIOCGIFFLAGS:
        if ((ifp = ifunit_ref((char *)data)) == NULL)
            return (EINVAL);
        linux_gifflags(ifp, (struct l_ifreq *)data);
        break;

    case SIOCSIFNAME:
        error = ENOIOCTL;
        break;

    case SIOCGIFHWADDR:
        if ((ifp = ifunit_ref((char *)data)) == NULL)
            return (EINVAL);
        error = linux_gifhwaddr(ifp, (struct l_ifreq *)data);
        break;

    default:
        error = fp->bsd_ioctl(cmd, data);
        break;
    }
    if (ifp)
        if_rele(ifp);
    return (error);
}
