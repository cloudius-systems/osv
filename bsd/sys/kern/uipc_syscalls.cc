/*-
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * sendfile(2) and related extensions:
 * Copyright (c) 1998, David Greenman. All rights reserved.
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
 *	@(#)uipc_syscalls.c	8.4 (Berkeley) 2/21/94
 */

#include <sys/cdefs.h>

#include <bsd/porting/netport.h>
#include <bsd/uipc_syscalls.h>

#include <fcntl.h>
#include <osv/fcntl.h>
#include <osv/ioctl.h>
#include <errno.h>

#include <bsd/sys/sys/param.h>
#include <bsd/porting/synch.h>
#include <osv/file.h>
#include <osv/socket.hh>

#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/protosw.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <osv/uio.h>
#include <bsd/sys/net/vnet.h>

#include <memory>
#include <fs/fs.hh>

using namespace std;

/* FIXME: OSv - implement... */
#if 0
static int do_sendfile(struct thread *td, struct sendfile_args *uap, int compat);
static int getsockname1(struct thread *td, struct getsockname_args *uap,
			int compat);
#endif

/*
 * NSFBUFS-related variables and associated sysctls
 */
int nsfbufs;
int nsfbufspeak;
int nsfbufsused;

SYSCTL_INT(_kern_ipc, OID_AUTO, nsfbufs, CTLFLAG_RDTUN, &nsfbufs, 0,
    "Maximum number of sendfile(2) sf_bufs available");
SYSCTL_INT(_kern_ipc, OID_AUTO, nsfbufspeak, CTLFLAG_RD, &nsfbufspeak, 0,
    "Number of sendfile(2) sf_bufs at peak usage");
SYSCTL_INT(_kern_ipc, OID_AUTO, nsfbufsused, CTLFLAG_RD, &nsfbufsused, 0,
    "Number of sendfile(2) sf_bufs in use");


/*
 * Convert a user file descriptor to a kernel file entry.
 * A reference on the file entry is held upon returning.
 */
static int
getsock_cap(int fd, struct file **fpp, u_int *fflagp)
{
    struct file *fp;
    int error;

    fp = NULL;
    error = fget(fd, &fp);
    if (error)
        return (error);

    if (file_type(fp) != DTYPE_SOCKET) {
        fdrop(fp);
        return (ENOTSOCK);
    }
    if (fflagp != NULL)
        *fflagp = file_flags(fp);
    *fpp = fp;
    return (0);
}

socketref socreate(int dom, int type, int proto)
{
	socket* so;
	int error = socreate(dom, &so, type, proto, nullptr, nullptr);
	if (error) {
		throw error;
	}
	return socketref(so);
}

/*
 * System call interface to the socket abstraction.
 */

int
sys_socket(int domain, int type, int protocol, int *out_fd)
{
	try {
		auto so = socreate(domain, type, protocol);
		fileref fp = make_file<socket_file>(FREAD | FWRITE, move(so));
		fdesc fd(fp);
		*out_fd = fd.release();
		return 0;
	} catch (int error) {
		return error;
	}
}

/* ARGSUSED */
int
sys_bind(int s, struct bsd_sockaddr *sa, int namelen)
{
	int error;

	sa->sa_len = namelen;
	error = kern_bind(s, sa);
	return (error);
}

int
kern_bind(int fd, struct bsd_sockaddr *sa)
{
	struct socket *so;
	struct file *fp;
	int error;

	error = getsock_cap(fd, &fp, NULL);
	if (error)
		return (error);

	so = (socket*)file_data(fp);
	error = sobind(so, sa, 0);
	fdrop(fp);
	return (error);
}

/* ARGSUSED */
int
sys_listen(int s, int backlog)
{
	struct socket *so;
	struct file *fp;
	int error;

	error = getsock_cap(s, &fp, NULL);
	if (error) {
	    return error;
	}

	so = (socket*)file_data(fp);
	error = solisten(so, backlog, 0);
	fdrop(fp);
	return(error);
}

/*
 * accept1()
 */
static int
accept1(int s,
        struct bsd_sockaddr * name,
        socklen_t * namelen, int *out_fd)
{
	int error;

	if (name == NULL)
		return (kern_accept(s, NULL, NULL, NULL, out_fd));

	error = kern_accept(s, name, namelen, NULL, out_fd);

	return (error);
}

int
kern_accept(int s, struct bsd_sockaddr *name,
    socklen_t *namelen, struct file **out_fp, int *out_fd)
{
	struct file *headfp, *nfp = NULL;
	struct bsd_sockaddr *sa = NULL;
	int error;
	struct socket *head, *so;
	int fd;
	u_int fflag;
	int tmp;

	if ((name) && (*namelen < 0)) {
			return (EINVAL);
	}

	error = getsock_cap(s, &headfp, &fflag);
	if (error)
		return (error);
	head = (socket*)file_data(headfp);
	if ((head->so_options & SO_ACCEPTCONN) == 0) {
		error = EINVAL;
		goto done;
	}
	ACCEPT_LOCK();
	if ((head->so_state & SS_NBIO) && TAILQ_EMPTY(&head->so_comp)) {
		ACCEPT_UNLOCK();
		error = EWOULDBLOCK;
		goto noconnection;
	}
	while (TAILQ_EMPTY(&head->so_comp) && head->so_error == 0) {
		if (head->so_rcv.sb_state & SBS_CANTRCVMORE) {
			head->so_error = ECONNABORTED;
			break;
		}
		error = msleep(&head->so_timeo, &accept_mtx, 0, "accept", 0);
		if (error) {
			ACCEPT_UNLOCK();
			goto noconnection;
		}
	}
	if (head->so_error) {
		error = head->so_error;
		head->so_error = 0;
		ACCEPT_UNLOCK();
		goto noconnection;
	}
	so = TAILQ_FIRST(&head->so_comp);
	KASSERT(!(so->so_qstate & SQ_INCOMP), ("accept1: so SQ_INCOMP"));
	KASSERT(so->so_qstate & SQ_COMP, ("accept1: so not SQ_COMP"));

	/*
	 * Before changing the flags on the socket, we have to bump the
	 * reference count.  Otherwise, if the protocol calls sofree(),
	 * the socket will be released due to a zero refcount.
	 */
	SOCK_LOCK(so);			/* soref() and so_state update */
	soref(so);			/* file descriptor reference */

	TAILQ_REMOVE(&head->so_comp, so, so_list);
	head->so_qlen--;
	so->so_state |= (head->so_state & SS_NBIO);
	so->so_qstate &= ~SQ_COMP;
	so->so_head = NULL;

	SOCK_UNLOCK(so);
	ACCEPT_UNLOCK();

	/* FIXME: OSv - Implement... select/poll */
#if 0
	/* connection has been removed from the listen queue */
	KNOTE_UNLOCKED(&head->so_rcv.sb_sel.si_note, 0);

	pgid = fgetown(&head->so_sigio);
	if (pgid != 0)
		fsetown(pgid, &so->so_sigio);
#endif
	try {
	    auto nf = make_file<socket_file>(fflag, so);
	    nfp = nf.get();  // want nf.release()
	    fhold(nfp);
	} catch (int err) {
	    error = err;
	    goto noconnection;
	}
	/* Sync socket nonblocking/async state with file flags */
	tmp = fflag & FNONBLOCK;
	(void) nfp->ioctl(FIONBIO, &tmp);
	tmp = fflag & FASYNC;
	(void) nfp->ioctl(FIOASYNC, &tmp);
	sa = 0;
	error = soaccept(so, &sa);
	if (error)
		goto noconnection;
	if (sa == NULL)
		goto done;
	if (name) {
		/* check sa_len before it is destroyed */
		if (*namelen > sa->sa_len)
			*namelen = sa->sa_len;
		bcopy(sa, name, *namelen);
	}
	error = fdalloc(nfp, &fd);
	if (error)
		goto noconnection;
	/* An extra reference on `nfp' has been held for us by fdalloc(). */
	*out_fd = fd;

noconnection:
	if (sa)
		free(sa);

	/*
	 * Release explicitly held references before returning.  We return
	 * a reference on nfp to the caller on success if they request it.
	 */
done:
	if (out_fp != NULL) {
		if (error == 0) {
			*out_fp = nfp;
			nfp = NULL;
		} else
			*out_fp = NULL;
	}
	if (nfp != NULL)
		fdrop(nfp);
	fdrop(headfp);
	return (error);
}

int
sys_accept(int s,
           struct bsd_sockaddr * name,
           socklen_t * namelen, int *out_fd)
{

	return (accept1(s, name, namelen, out_fd));
}

/* ARGSUSED */
int
sys_connect(int s, struct bsd_sockaddr *sa, socklen_t len)
{
	int error;

	sa->sa_len = len;
	error = kern_connect(s, sa);
	return (error);
}

int
kern_connect(int fd, struct bsd_sockaddr *sa)
{
	struct socket *so;
	struct file *fp;
	int error;
	int interrupted = 0;

	error = getsock_cap(fd, &fp, NULL);
	if (error)
		return (error);
	so = (socket*)file_data(fp);
	if (so->so_state & SS_ISCONNECTING) {
		error = EALREADY;
		goto done1;
	}
	error = soconnect(so, sa, 0);
	if (error)
		goto bad;
	if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING)) {
		error = EINPROGRESS;
		goto done1;
	}
	SOCK_LOCK(so);
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		error = msleep(&so->so_timeo, SOCK_MTX(so), 0,
		    "connec", 0);
		if (error) {
			if (error == EINTR || error == ERESTART)
				interrupted = 1;
			break;
		}
	}
	if (error == 0) {
		error = so->so_error;
		so->so_error = 0;
	}
	SOCK_UNLOCK(so);
bad:
	if (!interrupted)
		so->so_state &= ~SS_ISCONNECTING;
	if (error == ERESTART)
		error = EINTR;
done1:
	fdrop(fp);
	return (error);
}

int
kern_socketpair(int domain, int type, int protocol, int *rsv)
{
	try {
		socketref so1 = socreate(domain, type, protocol);
		socketref so2 = socreate(domain, type, protocol);
		int error = soconnect2(so1.get(), so2.get());
		if (error)
			return error;
		if (type == SOCK_DGRAM) {
			/*
			 * Datagram socket connection is asymmetric.
			 */
			 error = soconnect2(so2.get(), so1.get());
			 if (error)
				 return error;
		}
		fileref fp1 = make_file<socket_file>(FREAD | FWRITE, move(so1));
		fileref fp2 = make_file<socket_file>(FREAD | FWRITE, move(so2));
		fdesc fd1(fp1);
		fdesc fd2(fp2);
		// end of exception territory; relax
		rsv[0] = fd1.release();
		rsv[1] = fd2.release();
		return 0;
	} catch (int error) {
		return error;
	}
}

int
sys_socketpair(int domain, int type, int protocol, int *rsv)
{

	return (kern_socketpair(domain, type, protocol, rsv));
}

static int
sendit(int s, struct msghdr* mp, int flags, ssize_t* bytes)
{
	struct mbuf *control;
	struct bsd_sockaddr *to;
	int error;

	if (mp->msg_name != NULL) {
	    to = (struct bsd_sockaddr *)mp->msg_name;
	} else {
		to = NULL;
	}
	(void)to;  // FIXME: never used?

	if (mp->msg_control) {
		if (mp->msg_controllen < sizeof(struct cmsghdr)) {
			error = EINVAL;
			goto bad;
		}
		error = sockargs(&control, (caddr_t)mp->msg_control,
		    mp->msg_controllen, MT_CONTROL);
		if (error)
			goto bad;
	} else {
		control = NULL;
	}

	error = kern_sendit(s, mp, flags, control, bytes);

bad:
	return (error);
}

int
kern_sendit(int s,
            struct msghdr *mp,
            int flags,
            struct mbuf *control,
            ssize_t *bytes)
{
	struct file *fp;
	struct uio auio = {};
	struct iovec *iov;
	struct socket *so;
	struct bsd_sockaddr *from = 0;
	int i, error;
	ssize_t len;

	error = getsock_cap(s, &fp, NULL);
	if (error)
		return (error);
	so = (struct socket *)file_data(fp);

	// Create a local copy of the user's iovec - sosend() is going to change it!
	std::vector<iovec> uio_iov(mp->msg_iov, mp->msg_iov + mp->msg_iovlen);

	auio.uio_iov = uio_iov.data();
	auio.uio_iovcnt = uio_iov.size();
	auio.uio_rw = UIO_WRITE;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	iov = mp->msg_iov;
	for (i = 0; i < mp->msg_iovlen; i++, iov++) {
		if ((auio.uio_resid += iov->iov_len) < 0) {
			error = EINVAL;
			goto bad;
		}
	}
	len = auio.uio_resid;
	from = (struct bsd_sockaddr*)mp->msg_name;
	error = sosend(so, from, &auio, 0, control, flags, 0);
	if (error) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		/* FIXME: OSv - Implement... */
#if 0
		/* Generation of SIGPIPE can be controlled per socket */
		if (error == EPIPE && !(so->so_options & SO_NOSIGPIPE) &&
		    !(flags & MSG_NOSIGNAL)) {
			PROC_LOCK(td->td_proc);
			tdsignal(td, SIGPIPE);
			PROC_UNLOCK(td->td_proc);
		}
#endif
	}
	if (error == 0)
	    *bytes = len - auio.uio_resid;
bad:
	fdrop(fp);
	return (error);
}

int
sys_sendto(int s, caddr_t buf, size_t  len, int flags, caddr_t to, int tolen,
    ssize_t* bytes)
{
	struct msghdr msg = {};
	struct iovec aiov = {};
	int error;

	msg.msg_name = to;
	msg.msg_namelen = tolen;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = 0;

	aiov.iov_base = buf;
	aiov.iov_len = len;
	error = sendit(s, &msg, flags, bytes);
	return (error);
}

int
sys_sendmsg(int s, struct msghdr* msg, int flags, ssize_t* bytes)
{
	int error;

	error = sendit(s, msg, flags, bytes);
	return (error);
}

int
kern_recvit(int s, struct msghdr *mp, struct mbuf **controlp, ssize_t* bytes)
{
	struct uio auio;
	struct iovec *iov;
	int i;
	ssize_t len;
	int error;
	struct mbuf *m, *control = 0;
	caddr_t ctlbuf;
	struct file *fp;
	struct socket *so;
	struct bsd_sockaddr *fromsa = 0;

	if (controlp != NULL)
		*controlp = NULL;

	error = getsock_cap(s, &fp, NULL);
	if (error)
		return (error);
	so = (socket*)file_data(fp);

	auio.uio_iov = mp->msg_iov;
	auio.uio_iovcnt = mp->msg_iovlen;
	auio.uio_rw = UIO_READ;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	iov = mp->msg_iov;
	for (i = 0; i < mp->msg_iovlen; i++, iov++) {
		if ((auio.uio_resid += iov->iov_len) < 0) {
			fdrop(fp);
			return (EINVAL);
		}
	}
	len = auio.uio_resid;
	error = soreceive(so, &fromsa, &auio, (struct mbuf **)0,
	    (mp->msg_control || controlp) ? &control : (struct mbuf **)0,
	    &mp->msg_flags);
	if (error) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	}
	if (error)
		goto out;
	*bytes = len - auio.uio_resid;
	if (mp->msg_name) {
		len = mp->msg_namelen;
		if (len <= 0 || fromsa == 0)
			len = 0;
		else {
			/* save sa_len before it is destroyed by MSG_COMPAT */
			len = MIN(len, fromsa->sa_len);
			bcopy(fromsa, mp->msg_name, len);
		}
		mp->msg_namelen = len;
	}
	if (mp->msg_control && controlp == NULL) {
		len = mp->msg_controllen;
		m = control;
		mp->msg_controllen = 0;
		ctlbuf = (caddr_t)mp->msg_control;

		while (m && len > 0) {
			unsigned int tocopy;

			if (len >= m->m_hdr.mh_len)
				tocopy = m->m_hdr.mh_len;
			else {
				mp->msg_flags |= MSG_CTRUNC;
				tocopy = len;
			}

			if ((error = copyout(mtod(m, caddr_t),
					ctlbuf, tocopy)) != 0)
				goto out;

			ctlbuf += tocopy;
			len -= tocopy;
			m = m->m_hdr.mh_next;
		}
		mp->msg_controllen = ctlbuf - (caddr_t)mp->msg_control;
	}
out:
	fdrop(fp);
	if (fromsa)
		free(fromsa);

	if (error == 0 && controlp != NULL)  
		*controlp = control;
	else  if (control)
		m_freem(control);

	return (error);
}

static int
recvit(int s, struct msghdr *mp, void *namelenp, ssize_t* bytes)
{
	int error;

	error = kern_recvit(s, mp, NULL, bytes);
	if (error)
		return (error);
	if (namelenp) {
		error = copyout(&mp->msg_namelen, namelenp, sizeof (socklen_t));
	}
	return (error);
}

int
sys_recvfrom(int s, caddr_t buf, size_t  len, int flags,
    struct bsd_sockaddr * __restrict    from,
    socklen_t * __restrict fromlenaddr,
    ssize_t* bytes)
{
	struct msghdr msg;
	struct iovec aiov;
	int error;

	if (fromlenaddr) {
		error = copyin(fromlenaddr,
		    &msg.msg_namelen, sizeof (msg.msg_namelen));
		if (error)
			goto done2;
	} else {
		msg.msg_namelen = 0;
	}
	msg.msg_name = from;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = buf;
	aiov.iov_len = len;
	msg.msg_control = 0;
	msg.msg_flags = flags;
	error = recvit(s, &msg, fromlenaddr, bytes);
done2:
	return(error);
}

int
sys_recvmsg(int s, struct msghdr *msg, int flags, ssize_t* bytes)
{
	int error;

	msg->msg_flags = flags;
	error = recvit(s, msg, NULL, bytes);
	return (error);
}

/* ARGSUSED */
int
sys_shutdown(int s, int how)
{
	struct socket *so;
	struct file *fp;
	int error;

	error = getsock_cap(s, &fp, NULL);
	if (error == 0) {
		so = (socket*)file_data(fp);
		error = soshutdown(so, how);
		fdrop(fp);
	}
	return (error);
}

/* ARGSUSED */
int
sys_setsockopt(int s, int level, int name, caddr_t val, int valsize)
{

    return (kern_setsockopt(s, level, name, val, valsize));
}

int
kern_setsockopt(int s, int level, int name, void *val, socklen_t valsize)
{
	int error;
	struct socket *so;
	struct file *fp;
	struct sockopt sopt;

	if (val == NULL && valsize != 0)
		return (EFAULT);
	if ((int)valsize < 0)
		return (EINVAL);

	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_level = level;
	sopt.sopt_name = name;
	sopt.sopt_val = val;
	sopt.sopt_valsize = valsize;
	sopt.sopt_td = NULL;

	error = getsock_cap(s, &fp, NULL);
	if (error == 0) {
		so = (socket*)file_data(fp);
		error = sosetopt(so, &sopt);
		fdrop(fp);
	}
	return(error);
}

/* ARGSUSED */
int
sys_getsockopt(int s,
               int level,
               int name,
               void * __restrict val,
               socklen_t * __restrict avalsize)
{
	socklen_t valsize;
	int	error;

	if (val) {
		error = copyin(avalsize, &valsize, sizeof (valsize));
		if (error)
			return (error);
	}

	error = kern_getsockopt(s, level, name, val, &valsize);

	if (error == 0)
		error = copyout(&valsize, avalsize, sizeof (valsize));
	return (error);
}

/*
 * Kernel version of getsockopt.
 * optval can be a userland or userspace. optlen is always a kernel pointer.
 */
int
kern_getsockopt(int s,
                int level,
                int name,
                void *val,
                socklen_t *valsize)
{
	int error;
	struct  socket *so;
	struct file *fp;
	struct	sockopt sopt;

	if (val == NULL)
		*valsize = 0;
	if ((int)*valsize < 0)
		return (EINVAL);

	sopt.sopt_dir = SOPT_GET;
	sopt.sopt_level = level;
	sopt.sopt_name = name;
	sopt.sopt_val = val;
	sopt.sopt_valsize = (size_t)*valsize; /* checked non-negative above */
	sopt.sopt_td = NULL;

	error = getsock_cap(s, &fp, NULL);
	if (error == 0) {
		so = (socket*)file_data(fp);
		error = sogetopt(so, &sopt);
		*valsize = sopt.sopt_valsize;
		fdrop(fp);
	}
	return (error);
}

/*
 * getsockname1() - Get socket name.
 */
/* ARGSUSED */
int
getsockname1(int fdes, struct bsd_sockaddr * __restrict asa, socklen_t * __restrict alen)
{
	struct bsd_sockaddr *sa;
	socklen_t len = *alen;
	int error;

	error = kern_getsockname(fdes, &sa, &len);
	if (error) {
		*alen = 0;
		return (error);
	}

	*alen = len;
	if (len != 0) {
		bcopy(sa, asa, len);
	}
	free(sa);
	return (error);
}

int
kern_getsockname(int fd, struct bsd_sockaddr **sa, socklen_t *alen)
{
	struct socket *so;
	struct file *fp;
	socklen_t len;
	int error;

	if (*alen < 0)
		return (EINVAL);

	error = getsock_cap(fd, &fp, NULL);
	if (error)
		return (error);
	so = (socket*)file_data(fp);
	*sa = NULL;
	CURVNET_SET(so->so_vnet);
	error = (*so->so_proto->pr_usrreqs->pru_sockaddr)(so, sa);
	CURVNET_RESTORE();
	if (error)
		goto bad;
	if (*sa == NULL)
		len = 0;
	else
		len = MIN(*alen, (*sa)->sa_len);
	*alen = len;
#ifdef KTRACE
	if (KTRPOINT(td, KTR_STRUCT))
		ktrbsd_sockaddr(*sa);
#endif
bad:
	fdrop(fp);
	if (error && *sa) {
		free(*sa);
		*sa = NULL;
	}
	return (error);
}

int
sys_getsockname(int fdes, struct bsd_sockaddr * __restrict asa, socklen_t * __restrict alen)
{

	return (getsockname1(fdes, asa, alen));
}

/* FIXME: OSv - Implement getsockopt... */
#if 0

#ifdef COMPAT_OLDSOCK
int
ogetsockname(td, uap)
	struct thread *td;
	struct getsockname_args *uap;
{

	return (getsockname1(td, uap, 1));
}
#endif /* COMPAT_OLDSOCK */

#endif

static int
kern_getpeername(int fd, struct bsd_sockaddr **sa,
    socklen_t *alen)
{
	struct socket *so;
	struct file *fp;
	socklen_t len;
	int error;

	if (*alen < 0)
		return (EINVAL);

	error = getsock_cap(fd, &fp, NULL);
	if (error)
		return (error);
	so = (socket*)file_data(fp);
	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0) {
		error = ENOTCONN;
		goto done;
	}
	*sa = NULL;
	CURVNET_SET(so->so_vnet);
	error = (*so->so_proto->pr_usrreqs->pru_peeraddr)(so, sa);
	CURVNET_RESTORE();
	if (error)
		goto bad;
	if (*sa == NULL)
		len = 0;
	else
		len = MIN(*alen, (*sa)->sa_len);
	*alen = len;
bad:
	if (error && *sa) {
		free(*sa);
		*sa = NULL;
	}
done:
	fdrop(fp);
	return (error);
}

int
sys_getpeername(int fdes, struct bsd_sockaddr * __restrict asa, socklen_t * __restrict alen)
{
	struct bsd_sockaddr *sa;
	socklen_t len;
	int error;

	error = copyin(alen, &len, sizeof (len));
	if (error)
		return (error);

	error = kern_getpeername(fdes, &sa, &len);
	if (error)
		return (error);

	if (len != 0) {
		error = copyout(sa, asa, (u_int)len);
	}
	free(sa);
	if (error == 0)
		error = copyout(&len, alen, sizeof(len));
	return (error);
}

int
sockargs(struct mbuf **mp, caddr_t buf, int buflen, int type)
{
	struct bsd_sockaddr *sa;
	struct mbuf *m;
	int error;

	if ((u_int)buflen > MLEN) {
        if ((u_int)buflen > MCLBYTES)
            return (EINVAL);
	}
	m = m_get(M_WAIT, type);
	if ((u_int)buflen > MLEN)
		MCLGET(m, M_WAIT);
	m->m_hdr.mh_len = buflen;
	error = copyin(buf, mtod(m, caddr_t), (u_int)buflen);
	if (error)
		(void) m_free(m);
	else {
		*mp = m;
		if (type == MT_SONAME) {
			sa = mtod(m, struct bsd_sockaddr *);
			sa->sa_len = buflen;
		}
	}
	return (error);
}
