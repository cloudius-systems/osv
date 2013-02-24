#include <errno.h>
#include <memory.h>

#include <sys/time.h>
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

int ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps)
{
    struct timeval now;
    getmicrotime(&now);
    uint64_t now2 = now.tv_sec * hz + now.tv_usec;

    /*
     * Reset the last time and counter if this is the first call
     * or more than a second has passed since the last update of
     * lasttime.
     */
    if (lasttime->tv_sec == 0 || (u_int)(now2 - lasttime->tv_sec) >= hz) {
        lasttime->tv_sec = now2;
        *curpps = 1;
        return (maxpps != 0);
    } else {
        (*curpps)++;        /* NB: ignore potential overflow */
        return (maxpps < 0 || *curpps < maxpps);
    }
}

static void
timevalfix(struct timeval *t1)
{

    if (t1->tv_usec < 0) {
        t1->tv_sec--;
        t1->tv_usec += 1000000000;
    }
    if (t1->tv_usec >= 1000000000) {
        t1->tv_sec++;
        t1->tv_usec -= 1000000000;
    }
}

static void
timevalsub(struct timeval *t1, const struct timeval *t2)
{

    t1->tv_sec -= t2->tv_sec;
    t1->tv_usec -= t2->tv_usec;
    timevalfix(t1);
}

#define timevalcmp(tvp, uvp, cmp)                   \
    (((tvp)->tv_sec == (uvp)->tv_sec) ?             \
        ((tvp)->tv_usec cmp (uvp)->tv_usec) :           \
        ((tvp)->tv_sec cmp (uvp)->tv_sec))

int ratecheck(struct timeval *lasttime, const struct timeval *mininterval)
{
    struct timeval tv, delta;
    int rv = 0;

    getmicrotime(&tv);
    timevalsub(&delta, lasttime);

    /*
     * check for 0,0 is so that the message will be seen at least once,
     * even if interval is huge.
     */
    if (timevalcmp(&delta, mininterval, >=) ||
        (lasttime->tv_sec == 0 && lasttime->tv_usec == 0)) {
        *lasttime = tv;
        rv = 1;
    }

    return (rv);
}
