#include <errno.h>
#include <memory.h>

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/net/if_var.h>

int copyin(const void *uaddr, void *kaddr, size_t len)
{
    memcpy(kaddr, uaddr, len);
    return (0);
}

int copyout(const void *kaddr, void *uaddr, size_t len)
{
    memcpy(uaddr, kaddr, len);
    return (0);
}

int copystr(const void *kfaddr, void *kdaddr, size_t len, size_t *done)
{
    // FIXME: implement
    return (0);
}

int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
    // FIXME: implement
    return (0);
}


int priv_check(struct thread *td, int priv)
{
    return 1;
}



/*
 * Some routines that return EOPNOTSUPP for entry points that are not
 * supported by a protocol.  Fill in as needed.
 */
int
pru_accept_notsupp(struct socket *so, struct sockaddr **nam)
{

    return EOPNOTSUPP;
}

int
pru_attach_notsupp(struct socket *so, int proto, struct thread *td)
{

    return EOPNOTSUPP;
}

int
pru_bind_notsupp(struct socket *so, struct sockaddr *nam, struct thread *td)
{

    return EOPNOTSUPP;
}

int
pru_connect_notsupp(struct socket *so, struct sockaddr *nam, struct thread *td)
{

    return EOPNOTSUPP;
}

int
pru_connect2_notsupp(struct socket *so1, struct socket *so2)
{

    return EOPNOTSUPP;
}

int
pru_control_notsupp(struct socket *so, u_long cmd, caddr_t data,
    struct ifnet *ifp, struct thread *td)
{

    return EOPNOTSUPP;
}

int
pru_disconnect_notsupp(struct socket *so)
{

    return EOPNOTSUPP;
}

int
pru_listen_notsupp(struct socket *so, int backlog, struct thread *td)
{

    return EOPNOTSUPP;
}

int
pru_peeraddr_notsupp(struct socket *so, struct sockaddr **nam)
{

    return EOPNOTSUPP;
}

int
pru_rcvd_notsupp(struct socket *so, int flags)
{

    return EOPNOTSUPP;
}

int
pru_rcvoob_notsupp(struct socket *so, struct mbuf *m, int flags)
{

    return EOPNOTSUPP;
}

int
pru_send_notsupp(struct socket *so, int flags, struct mbuf *m,
    struct sockaddr *addr, struct mbuf *control, struct thread *td)
{

    return EOPNOTSUPP;
}

/*
 * This isn't really a ``null'' operation, but it's the default one and
 * doesn't do anything destructive.
 */
struct stat;
int
pru_sense_null(struct socket *so, struct stat *sb)
{
//    sb->st_blksize = so->so_snd.sb_hiwat;
    return 0;
}

int
pru_shutdown_notsupp(struct socket *so)
{

    return EOPNOTSUPP;
}

int
pru_sockaddr_notsupp(struct socket *so, struct sockaddr **nam)
{

    return EOPNOTSUPP;
}

int
pru_sosend_notsupp(struct socket *so, struct sockaddr *addr, struct uio *uio,
    struct mbuf *top, struct mbuf *control, int flags, struct thread *td)
{

    return EOPNOTSUPP;
}

int
pru_soreceive_notsupp(struct socket *so, struct sockaddr **paddr,
    struct uio *uio, struct mbuf **mp0, struct mbuf **controlp, int *flagsp)
{

    return EOPNOTSUPP;
}

int
pru_sopoll_notsupp(struct socket *so, int events, struct ucred *cred,
    struct thread *td)
{

    return EOPNOTSUPP;
}
