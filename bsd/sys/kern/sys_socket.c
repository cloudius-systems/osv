/*-
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *
 *  @(#)sys_socket.c    8.1 (Berkeley) 6/10/93
 */

#include <sys/cdefs.h>

#include <sys/stat.h>
#include <osv/file.h>
#include <osv/uio.h>
#include <osv/types.h>
#include <osv/ioctl.h>
#include <fs/unsupported.h>

#include <bsd/sys/sys/libkern.h>
#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/protosw.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/net/vnet.h>

extern int linux_ioctl_socket(struct file *fp, u_long cmd, void *data) ;

int
soo_init(struct file *fp)
{
    struct socket *so = file_data(fp);
    so->fp = fp;

    return 0;
}

/* ARGSUSED */
int
soo_read(struct file *fp, struct uio *uio, int flags)
{
    struct socket *so = file_data(fp);
    int error;

    error = soreceive(so, 0, uio, 0, 0, 0);
    return (error);
}

/* ARGSUSED */
int
soo_write(struct file *fp, struct uio *uio, int flags)
{
    struct socket *so = file_data(fp);
    int error;

    error = sosend(so, 0, uio, 0, 0, 0, 0);
#if 0
    if (error == EPIPE && (so->so_options & SO_NOSIGPIPE) == 0) {
        PROC_LOCK(uio->uio_td->td_proc);
        tdsignal(uio->uio_td, SIGPIPE);
        PROC_UNLOCK(uio->uio_td->td_proc);
    }
#endif
    return (error);
}

int
soo_truncate(struct file *fp, off_t length)
{

    return (EINVAL);
}


#define LINUX_IOCTL_IDX(ioctl)  ((ioctl) - SIOCBEGIN)
#define LINUX_IOCTL_TYPE(ioctl)  (linux_ioctl_type_tbl[LINUX_IOCTL_IDX(ioctl)])

static char linux_ioctl_type_tbl[256] = {
    [LINUX_IOCTL_IDX(SIOCGIFCONF)]        = 'i', /* get iface list           */
    [LINUX_IOCTL_IDX(SIOCGIFFLAGS)]       = 'i', /* get flags                */
    [LINUX_IOCTL_IDX(SIOCSIFFLAGS)]       = 'i', /* set flags                */
    [LINUX_IOCTL_IDX(SIOCGIFADDR)]        = 'i', /* get PA address           */
    [LINUX_IOCTL_IDX(SIOCSIFADDR)]        = 'i', /* set PA address           */
    [LINUX_IOCTL_IDX(SIOCGIFDSTADDR)]     = 'i', /* get remote PA address    */
    [LINUX_IOCTL_IDX(SIOCSIFDSTADDR)]     = 'i', /* set remote PA address    */
    [LINUX_IOCTL_IDX(SIOCGIFBRDADDR)]     = 'i', /* get broadcast PA address */
    [LINUX_IOCTL_IDX(SIOCSIFBRDADDR)]     = 'i', /* set broadcast PA address */
    [LINUX_IOCTL_IDX(SIOCGIFNETMASK)]     = 'i', /* get network PA mask      */
    [LINUX_IOCTL_IDX(SIOCSIFNETMASK)]     = 'i', /* set network PA mask      */
    [LINUX_IOCTL_IDX(SIOCGIFMETRIC)]      = 'i', /* get metric               */
    [LINUX_IOCTL_IDX(SIOCSIFMETRIC)]      = 'i', /* set metric               */
    [LINUX_IOCTL_IDX(SIOCGIFMTU)]         = 'i', /* get MTU size             */
    [LINUX_IOCTL_IDX(SIOCSIFMTU)]         = 'i', /* set MTU size             */
    [LINUX_IOCTL_IDX(SIOCSIFNAME)]        = 'i', /* set interface name       */
    [LINUX_IOCTL_IDX(SIOCGIFHWADDR)]      = 'i', /* set hardware address 	 */
    [LINUX_IOCTL_IDX(SIOCADDMULTI)]       = 'i', /* Multicast address lists  */
    [LINUX_IOCTL_IDX(SIOCDELMULTI)]       = 'i',
    [LINUX_IOCTL_IDX(SIOCGIFINDEX)]       = 'i', /* name -> if_index mapping */
    [LINUX_IOCTL_IDX(SIOCDIFADDR)]        = 'i', /* delete PA address        */
    [LINUX_IOCTL_IDX(SIOCGIFBR)]          = 'i', /* Bridging support         */
    [LINUX_IOCTL_IDX(SIOCSIFBR)]          = 'i', /* Set bridging options     */
} ;


static char get_ioctl_type(int ioctl)
{
    if (ioctl >= SIOCBEGIN && ioctl <= SIOCEND)
        return LINUX_IOCTL_TYPE(ioctl) ;
    else
        return _IOC_TYPE(ioctl) ;
}

int
soo_ioctl(struct file *fp, u_long cmd, void *data)
{
    struct socket *so = file_data(fp);
    int error = 0;
    char ioctl_type ;
    
    switch (cmd) {
    case FIONBIO:
        SOCK_LOCK(so);
        if (*(int *)data)
            so->so_state |= SS_NBIO;
        else
            so->so_state &= ~SS_NBIO;
        SOCK_UNLOCK(so);
        break;

    case FIOASYNC:
        /*
         * XXXRW: This code separately acquires SOCK_LOCK(so) and
         * SOCKBUF_LOCK(&so->so_rcv) even though they are the same
         * mutex to avoid introducing the assumption that they are
         * the same.
         */
        if (*(int *)data) {
            SOCK_LOCK(so);
            so->so_state |= SS_ASYNC;
            SOCK_UNLOCK(so);
            SOCKBUF_LOCK(&so->so_rcv);
            so->so_rcv.sb_flags |= SB_ASYNC;
            SOCKBUF_UNLOCK(&so->so_rcv);
            SOCKBUF_LOCK(&so->so_snd);
            so->so_snd.sb_flags |= SB_ASYNC;
            SOCKBUF_UNLOCK(&so->so_snd);
        } else {
            SOCK_LOCK(so);
            so->so_state &= ~SS_ASYNC;
            SOCK_UNLOCK(so);
            SOCKBUF_LOCK(&so->so_rcv);
            so->so_rcv.sb_flags &= ~SB_ASYNC;
            SOCKBUF_UNLOCK(&so->so_rcv);
            SOCKBUF_LOCK(&so->so_snd);
            so->so_snd.sb_flags &= ~SB_ASYNC;
            SOCKBUF_UNLOCK(&so->so_snd);
        }
        break;

    case FIONREAD:
        /* Unlocked read. */
        *(int *)data = so->so_rcv.sb_cc;
        break;

    case FIONWRITE:
        /* Unlocked read. */
        *(int *)data = so->so_snd.sb_cc;
        break;

    case FIONSPACE:
        if ((so->so_snd.sb_hiwat < so->so_snd.sb_cc) ||
            (so->so_snd.sb_mbmax < so->so_snd.sb_mbcnt))
            *(int *)data = 0;
        else
            *(int *)data = sbspace(&so->so_snd);
        break;

    case SIOCATMARK:
        /* Unlocked read. */
        *(int *)data = (so->so_rcv.sb_state & SBS_RCVATMARK) != 0;
        break;
    default:
        /*
         * Interface/routing/protocol specific ioctls: interface and
         * routing ioctls should have a different entry since a
         * socket is unnecessary.
         */
        ioctl_type = get_ioctl_type(cmd) ;
        if (ioctl_type == 'i')
            error = ifioctl(so, cmd, data, 0);
        else if (ioctl_type == 'r') {
            CURVNET_SET(so->so_vnet);
            error = rtioctl_fib(cmd, data, so->so_fibnum);
            CURVNET_RESTORE();
        } else if (ioctl_type == '\0') {
            error = ENOTTY ;    /* An unsupported Linux ioctl */
        } else {
            CURVNET_SET(so->so_vnet);
            error = ((*so->so_proto->pr_usrreqs->pru_control)
                (so, cmd, data, 0, 0));
            CURVNET_RESTORE();
        }
        break;
    }
    return (error);
}

int
soo_poll(struct file *fp, int events)
{
    struct socket *so = file_data(fp);
    return (sopoll(so, events, 0, 0));
}

int
soo_stat(struct file *fp, struct stat *ub)
{
    struct socket *so = file_data(fp);

    bzero((caddr_t)ub, sizeof (*ub));
    ub->st_mode = S_IFSOCK;
    /*
     * If SBS_CANTRCVMORE is set, but there's still data left in the
     * receive buffer, the socket is still readable.
     */
    SOCKBUF_LOCK(&so->so_rcv);
    if ((so->so_rcv.sb_state & SBS_CANTRCVMORE) == 0 ||
        so->so_rcv.sb_cc != 0)
        ub->st_mode |= S_IRUSR | S_IRGRP | S_IROTH;
    ub->st_size = so->so_rcv.sb_cc - so->so_rcv.sb_ctl;
    SOCKBUF_UNLOCK(&so->so_rcv);
    /* Unlocked read. */
    if ((so->so_snd.sb_state & SBS_CANTSENDMORE) == 0)
        ub->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    ub->st_uid = 0;
    ub->st_gid = 0;
    return (*so->so_proto->pr_usrreqs->pru_sense)(so, ub);
}

/*
 * API socket close on file pointer.  We call soclose() to close the socket
 * (including initiating closing protocols).  soclose() will sorele() the
 * file reference but the actual socket will not go away until the socket's
 * ref count hits 0.
 */
/* ARGSUSED */
int
soo_close(struct file *fp)
{
    int error = 0;
    struct socket *so;

    so = file_data(fp);
    file_makebad(fp);

    if (so)
        error = soclose(so);
    return (error);
}

struct fileops socketops = {
    .fo_init = soo_init,
    .fo_read = soo_read,
    .fo_write = soo_write,
    .fo_truncate = soo_truncate,
    .fo_ioctl = linux_ioctl_socket,
    .fo_poll = soo_poll,
    .fo_stat = soo_stat,
    .fo_close = soo_close,
    .fo_chmod = unsupported_chmod,
};
