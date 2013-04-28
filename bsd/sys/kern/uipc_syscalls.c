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
#include <errno.h>

#include <bsd/sys/sys/param.h>
#include <bsd/porting/synch.h>
#include <osv/file.h>

#include <bsd/sys/sys/filio.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/protosw.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <osv/uio.h>
#include <bsd/sys/net/vnet.h>


/* FIXME: OSv - implement... */
#if 0
static int do_sendfile(struct thread *td, struct sendfile_args *uap, int compat);
static int getsockname1(struct thread *td, struct getsockname_args *uap,
			int compat);
static int getpeername1(struct thread *td, struct getpeername_args *uap,
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

    if (fp->f_type != DTYPE_SOCKET) {
        fdrop(fp);
        return (ENOTSOCK);
    }
    if (fflagp != NULL)
        *fflagp = fp->f_flags;
    *fpp = fp;
    return (0);
}

/*
 * System call interface to the socket abstraction.
 */

int
sys_socket(int domain, int type, int protocol, int *out_fd)
{
	struct socket *so;
	struct file *fp;
	int fd, error;

	error = falloc(&fp, &fd);
	if (error)
		return (error);
	/* An extra reference on `fp' has been held for us by falloc(). */
	error = socreate(domain, &so, type, protocol, 0, 0);
	if (error) {
		fdrop(fp);
	} else {
		finit(fp, FREAD | FWRITE, DTYPE_SOCKET, so, &socketops);
		*out_fd = fd;
	}
	fdrop(fp);
	return (error);
}

/* ARGSUSED */
int
sys_bind(int s, struct sockaddr *sa, int namelen)
{
	int error;

	sa->sa_len = namelen;
	error = kern_bind(s, sa);
	return (error);
}

int
kern_bind(int fd, struct sockaddr *sa)
{
	struct socket *so;
	struct file *fp;
	int error;

	error = getsock_cap(fd, &fp, NULL);
	if (error)
		return (error);

	so = fp->f_data;
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

	so = fp->f_data;
	error = solisten(so, backlog, 0);
	fdrop(fp);
	return(error);
}

/*
 * accept1()
 */
static int
accept1(int s,
        struct sockaddr * name,
        socklen_t * namelen, int *out_fp)
{
	struct file *fp;
	int error;

	if (name == NULL)
		return (kern_accept(s, NULL, NULL, NULL, out_fp));

    error = getsock_cap(s, &fp, NULL);
    if (error) {
        return error;
    }

	error = kern_accept(s, &name, namelen, &fp, out_fp);
	fdrop(fp);

	return (error);
}

int
kern_accept(int s, struct sockaddr **name,
    socklen_t *namelen, struct file **fp, int *out_fd)
{
	struct file *headfp, *nfp = NULL;
	struct sockaddr *sa = NULL;
	int error;
	struct socket *head, *so;
	int fd;
	u_int fflag;
	int tmp;

	if (name) {
		*name = NULL;
		if (*namelen < 0)
			return (EINVAL);
	}

	error = getsock_cap(s, &headfp, &fflag);
	if (error)
		return (error);
	head = headfp->f_data;
	if ((head->so_options & SO_ACCEPTCONN) == 0) {
		error = EINVAL;
		goto done;
	}
	error = falloc(&nfp, &fd);
	if (error)
		goto done;
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

	/* An extra reference on `nfp' has been held for us by falloc(). */
	*out_fd = fd;

	/* FIXME: OSv - Implement... select/poll */
#if 0
	/* connection has been removed from the listen queue */
	KNOTE_UNLOCKED(&head->so_rcv.sb_sel.si_note, 0);

	pgid = fgetown(&head->so_sigio);
	if (pgid != 0)
		fsetown(pgid, &so->so_sigio);
#endif

	finit(nfp, fflag, DTYPE_SOCKET, so, &socketops);
	/* Sync socket nonblocking/async state with file flags */
	tmp = fflag & FNONBLOCK;
	(void) fo_ioctl(nfp, FIONBIO, &tmp);
	tmp = fflag & FASYNC;
	(void) fo_ioctl(nfp, FIOASYNC, &tmp);
	sa = 0;
	error = soaccept(so, &sa);
	if (error) {
		/*
		 * return a namelen of zero for older code which might
		 * ignore the return value from accept.
		 */
		if (name)
			*namelen = 0;
		goto noconnection;
	}
	if (sa == NULL) {
		if (name)
			*namelen = 0;
		goto done;
	}
	if (name) {
		/* check sa_len before it is destroyed */
		if (*namelen > sa->sa_len)
			*namelen = sa->sa_len;
		*name = sa;
		sa = NULL;
	}
noconnection:
	if (sa)
		free(sa);

	/*
	 * close the new descriptor, assuming someone hasn't ripped it
	 * out from under us.
	 */
	if (error)
	    fdrop(nfp);

	/*
	 * Release explicitly held references before returning.  We return
	 * a reference on nfp to the caller on success if they request it.
	 */
done:
	if (fp != NULL) {
		if (error == 0) {
			*fp = nfp;
			nfp = NULL;
		} else
			*fp = NULL;
	}
	if (nfp != NULL)
		fdrop(nfp);
	fdrop(headfp);
	return (error);
}

int
sys_accept(int s,
           struct sockaddr * name,
           socklen_t * namelen, int *out_fd)
{

	return (accept1(s, name, namelen, out_fd));
}

/* ARGSUSED */
int
sys_connect(int s, struct sockaddr *sa, socklen_t len)
{
	int error;

	sa->sa_len = len;
	error = kern_connect(s, sa);
	return (error);
}

int
kern_connect(int fd, struct sockaddr *sa)
{
	struct socket *so;
	struct file *fp;
	int error;
	int interrupted = 0;

	error = getsock_cap(fd, &fp, NULL);
	if (error)
		return (error);
	so = fp->f_data;
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
	struct file *fp1, *fp2;
	struct socket *so1, *so2;
	int fd, error;

	error = socreate(domain, &so1, type, protocol, 0, 0);
	if (error)
		return (error);
	error = socreate(domain, &so2, type, protocol, 0, 0);
	if (error)
		goto free1;
	/* On success extra reference to `fp1' and 'fp2' is set by falloc. */
	error = falloc(&fp1, &fd);
	if (error)
		goto free2;
	rsv[0] = fd;
	fp1->f_data = so1;	/* so1 already has ref count */
	error = falloc(&fp2, &fd);
	if (error)
		goto free3;
	fp2->f_data = so2;	/* so2 already has ref count */
	rsv[1] = fd;
	error = soconnect2(so1, so2);
	if (error)
		goto free4;
	if (type == SOCK_DGRAM) {
		/*
		 * Datagram socket connection is asymmetric.
		 */
		 error = soconnect2(so2, so1);
		 if (error)
			goto free4;
	}
	finit(fp1, FREAD | FWRITE, DTYPE_SOCKET, fp1->f_data, &socketops);
	finit(fp2, FREAD | FWRITE, DTYPE_SOCKET, fp2->f_data, &socketops);
	fdrop(fp1);
	fdrop(fp2);
	return (0);
free4:
    fdrop(fp2);
	fdrop(fp2);
free3:
	fdrop(fp1);
	fdrop(fp1);
free2:
	if (so2 != NULL)
		(void)soclose(so2);
free1:
	if (so1 != NULL)
		(void)soclose(so1);
	return (error);
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
	struct sockaddr *to;
	int error;

	if (mp->msg_name != NULL) {
	    to = (struct sockaddr *)mp->msg_name;
	} else {
		to = NULL;
	}

	if (mp->msg_control) {
		if (mp->msg_controllen < sizeof(struct cmsghdr)) {
			error = EINVAL;
			goto bad;
		}
		error = sockargs(&control, mp->msg_control,
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
	struct uio auio;
	struct iovec *iov;
	struct socket *so;
	struct sockaddr *from = 0;
	int i, error;
	ssize_t len;

	error = getsock_cap(s, &fp, NULL);
	if (error)
		return (error);
	so = (struct socket *)fp->f_data;

	auio.uio_iov = mp->msg_iov;
	auio.uio_iovcnt = mp->msg_iovlen;
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
	from = (struct sockaddr*)mp->msg_name;
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
	struct msghdr msg;
	struct iovec aiov;
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
	struct sockaddr *fromsa = 0;

	if (controlp != NULL)
		*controlp = NULL;

	error = getsock_cap(s, &fp, NULL);
	if (error)
		return (error);
	so = fp->f_data;

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
		ctlbuf = mp->msg_control;

		while (m && len > 0) {
			unsigned int tocopy;

			if (len >= m->m_len)
				tocopy = m->m_len;
			else {
				mp->msg_flags |= MSG_CTRUNC;
				tocopy = len;
			}

			if ((error = copyout(mtod(m, caddr_t),
					ctlbuf, tocopy)) != 0)
				goto out;

			ctlbuf += tocopy;
			len -= tocopy;
			m = m->m_next;
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
    struct sockaddr * __restrict    from,
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
		so = fp->f_data;
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
		so = fp->f_data;
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
		so = fp->f_data;
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
getsockname1(int fdes, struct sockaddr * __restrict asa, socklen_t * __restrict alen)
{
	struct sockaddr *sa;
	socklen_t len;
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
kern_getsockname(int fd, struct sockaddr **sa, socklen_t *alen)
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
	so = fp->f_data;
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
		ktrsockaddr(*sa);
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
sys_getsockname(int fdes, struct sockaddr * __restrict asa, socklen_t * __restrict alen)
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

/*
 * getpeername1() - Get name of peer for connected socket.
 */
/* ARGSUSED */
static int
getpeername1(td, uap, compat)
	struct thread *td;
	struct getpeername_args /* {
		int	fdes;
		struct sockaddr * __restrict	asa;
		socklen_t * __restrict	alen;
	} */ *uap;
	int compat;
{
	struct sockaddr *sa;
	socklen_t len;
	int error;

	error = copyin(uap->alen, &len, sizeof (len));
	if (error)
		return (error);

	error = kern_getpeername(td, uap->fdes, &sa, &len);
	if (error)
		return (error);

	if (len != 0) {
#ifdef COMPAT_OLDSOCK
		if (compat)
			((struct osockaddr *)sa)->sa_family = sa->sa_family;
#endif
		error = copyout(sa, uap->asa, (u_int)len);
	}
	free(sa, M_SONAME);
	if (error == 0)
		error = copyout(&len, uap->alen, sizeof(len));
	return (error);
}

int
kern_getpeername(struct thread *td, int fd, struct sockaddr **sa,
    socklen_t *alen)
{
	struct socket *so;
	struct file *fp;
	socklen_t len;
	int error;

	if (*alen < 0)
		return (EINVAL);

	AUDIT_ARG_FD(fd);
	error = getsock_cap(td->td_proc->p_fd, fd, CAP_GETPEERNAME, &fp, NULL);
	if (error)
		return (error);
	so = fp->f_data;
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
#ifdef KTRACE
	if (KTRPOINT(td, KTR_STRUCT))
		ktrsockaddr(*sa);
#endif
bad:
	if (error && *sa) {
		free(*sa, M_SONAME);
		*sa = NULL;
	}
done:
	fdrop(fp, td);
	return (error);
}

int
sys_getpeername(td, uap)
	struct thread *td;
	struct getpeername_args *uap;
{

	return (getpeername1(td, uap, 0));
}
#endif

int
sockargs(struct mbuf **mp, caddr_t buf, int buflen, int type)
{
	struct sockaddr *sa;
	struct mbuf *m;
	int error;

	if ((u_int)buflen > MLEN) {
        if ((u_int)buflen > MCLBYTES)
            return (EINVAL);
	}
	m = m_get(M_WAIT, type);
	if ((u_int)buflen > MLEN)
		MCLGET(m, M_WAIT);
	m->m_len = buflen;
	error = copyin(buf, mtod(m, caddr_t), (u_int)buflen);
	if (error)
		(void) m_free(m);
	else {
		*mp = m;
		if (type == MT_SONAME) {
			sa = mtod(m, struct sockaddr *);
			sa->sa_len = buflen;
		}
	}
	return (error);
}

/* FIXME: OSv - Implement sendfile */
#if 0

#include <sys/condvar.h>

struct sendfile_sync {
	struct mtx	mtx;
	struct cv	cv;
	unsigned 	count;
};

/*
 * Detach mapped page and release resources back to the system.
 */
void
sf_buf_mext(void *addr, void *args)
{
	vm_page_t m;
	struct sendfile_sync *sfs;

	m = sf_buf_page(args);
	sf_buf_free(args);
	vm_page_lock(m);
	vm_page_unwire(m, 0);
	/*
	 * Check for the object going away on us. This can
	 * happen since we don't hold a reference to it.
	 * If so, we're responsible for freeing the page.
	 */
	if (m->wire_count == 0 && m->object == NULL)
		vm_page_free(m);
	vm_page_unlock(m);
	if (addr == NULL)
		return;
	sfs = addr;
	mtx_lock(&sfs->mtx);
	KASSERT(sfs->count> 0, ("Sendfile sync botchup count == 0"));
	if (--sfs->count == 0)
		cv_signal(&sfs->cv);
	mtx_unlock(&sfs->mtx);
}

/*
 * sendfile(2)
 *
 * int sendfile(int fd, int s, off_t offset, size_t nbytes,
 *	 struct sf_hdtr *hdtr, off_t *sbytes, int flags)
 *
 * Send a file specified by 'fd' and starting at 'offset' to a socket
 * specified by 's'. Send only 'nbytes' of the file or until EOF if nbytes ==
 * 0.  Optionally add a header and/or trailer to the socket output.  If
 * specified, write the total number of bytes sent into *sbytes.
 */
int
sys_sendfile(struct thread *td, struct sendfile_args *uap)
{

	return (do_sendfile(td, uap, 0));
}

static int
do_sendfile(struct thread *td, struct sendfile_args *uap, int compat)
{
	struct sf_hdtr hdtr;
	struct uio *hdr_uio, *trl_uio;
	int error;

	hdr_uio = trl_uio = NULL;

	if (uap->hdtr != NULL) {
		error = copyin(uap->hdtr, &hdtr, sizeof(hdtr));
		if (error)
			goto out;
		if (hdtr.headers != NULL) {
			error = copyinuio(hdtr.headers, hdtr.hdr_cnt, &hdr_uio);
			if (error)
				goto out;
		}
		if (hdtr.trailers != NULL) {
			error = copyinuio(hdtr.trailers, hdtr.trl_cnt, &trl_uio);
			if (error)
				goto out;

		}
	}

	error = kern_sendfile(td, uap, hdr_uio, trl_uio, compat);
out:
	if (hdr_uio)
		free(hdr_uio, M_IOV);
	if (trl_uio)
		free(trl_uio, M_IOV);
	return (error);
}

#ifdef COMPAT_FREEBSD4
int
freebsd4_sendfile(struct thread *td, struct freebsd4_sendfile_args *uap)
{
	struct sendfile_args args;

	args.fd = uap->fd;
	args.s = uap->s;
	args.offset = uap->offset;
	args.nbytes = uap->nbytes;
	args.hdtr = uap->hdtr;
	args.sbytes = uap->sbytes;
	args.flags = uap->flags;

	return (do_sendfile(td, &args, 1));
}
#endif /* COMPAT_FREEBSD4 */

int
kern_sendfile(struct thread *td, struct sendfile_args *uap,
    struct uio *hdr_uio, struct uio *trl_uio, int compat)
{
	struct file *sock_fp;
	struct vnode *vp;
	struct vm_object *obj = NULL;
	struct socket *so = NULL;
	struct mbuf *m = NULL;
	struct sf_buf *sf;
	struct vm_page *pg;
	off_t off, xfsize, fsbytes = 0, sbytes = 0, rem = 0;
	int error, hdrlen = 0, mnw = 0;
	int vfslocked;
	struct sendfile_sync *sfs = NULL;

	/*
	 * The file descriptor must be a regular file and have a
	 * backing VM object.
	 * File offset must be positive.  If it goes beyond EOF
	 * we send only the header/trailer and no payload data.
	 */
	AUDIT_ARG_FD(uap->fd);
	if ((error = fgetvp_read(td, uap->fd, CAP_READ, &vp)) != 0)
		goto out;
	vfslocked = VFS_LOCK_GIANT(vp->v_mount);
	vn_lock(vp, LK_SHARED | LK_RETRY);
	if (vp->v_type == VREG) {
		obj = vp->v_object;
		if (obj != NULL) {
			/*
			 * Temporarily increase the backing VM
			 * object's reference count so that a forced
			 * reclamation of its vnode does not
			 * immediately destroy it.
			 */
			VM_OBJECT_LOCK(obj);
			if ((obj->flags & OBJ_DEAD) == 0) {
				vm_object_reference_locked(obj);
				VM_OBJECT_UNLOCK(obj);
			} else {
				VM_OBJECT_UNLOCK(obj);
				obj = NULL;
			}
		}
	}
	VOP_UNLOCK(vp, 0);
	VFS_UNLOCK_GIANT(vfslocked);
	if (obj == NULL) {
		error = EINVAL;
		goto out;
	}
	if (uap->offset < 0) {
		error = EINVAL;
		goto out;
	}

	/*
	 * The socket must be a stream socket and connected.
	 * Remember if it a blocking or non-blocking socket.
	 */
	if ((error = getsock_cap(td->td_proc->p_fd, uap->s, CAP_WRITE,
	    &sock_fp, NULL)) != 0)
		goto out;
	so = sock_fp->f_data;
	if (so->so_type != SOCK_STREAM) {
		error = EINVAL;
		goto out;
	}
	if ((so->so_state & SS_ISCONNECTED) == 0) {
		error = ENOTCONN;
		goto out;
	}
	/*
	 * Do not wait on memory allocations but return ENOMEM for
	 * caller to retry later.
	 * XXX: Experimental.
	 */
	if (uap->flags & SF_MNOWAIT)
		mnw = 1;

	if (uap->flags & SF_SYNC) {
		sfs = malloc(sizeof *sfs, M_TEMP, M_WAITOK | M_ZERO);
		mtx_init(&sfs->mtx, "sendfile", NULL, MTX_DEF);
		cv_init(&sfs->cv, "sendfile");
	}

#ifdef MAC
	error = mac_socket_check_send(td->td_ucred, so);
	if (error)
		goto out;
#endif

	/* If headers are specified copy them into mbufs. */
	if (hdr_uio != NULL) {
		hdr_uio->uio_td = td;
		hdr_uio->uio_rw = UIO_WRITE;
		if (hdr_uio->uio_resid > 0) {
			/*
			 * In FBSD < 5.0 the nbytes to send also included
			 * the header.  If compat is specified subtract the
			 * header size from nbytes.
			 */
			if (compat) {
				if (uap->nbytes > hdr_uio->uio_resid)
					uap->nbytes -= hdr_uio->uio_resid;
				else
					uap->nbytes = 0;
			}
			m = m_uiotombuf(hdr_uio, (mnw ? M_NOWAIT : M_WAITOK),
			    0, 0, 0);
			if (m == NULL) {
				error = mnw ? EAGAIN : ENOBUFS;
				goto out;
			}
			hdrlen = m_length(m, NULL);
		}
	}

	/*
	 * Protect against multiple writers to the socket.
	 *
	 * XXXRW: Historically this has assumed non-interruptibility, so now
	 * we implement that, but possibly shouldn't.
	 */
	(void)sblock(&so->so_snd, SBL_WAIT | SBL_NOINTR);

	/*
	 * Loop through the pages of the file, starting with the requested
	 * offset. Get a file page (do I/O if necessary), map the file page
	 * into an sf_buf, attach an mbuf header to the sf_buf, and queue
	 * it on the socket.
	 * This is done in two loops.  The inner loop turns as many pages
	 * as it can, up to available socket buffer space, without blocking
	 * into mbufs to have it bulk delivered into the socket send buffer.
	 * The outer loop checks the state and available space of the socket
	 * and takes care of the overall progress.
	 */
	for (off = uap->offset, rem = uap->nbytes; ; ) {
		struct mbuf *mtail = NULL;
		int loopbytes = 0;
		int space = 0;
		int done = 0;

		/*
		 * Check the socket state for ongoing connection,
		 * no errors and space in socket buffer.
		 * If space is low allow for the remainder of the
		 * file to be processed if it fits the socket buffer.
		 * Otherwise block in waiting for sufficient space
		 * to proceed, or if the socket is nonblocking, return
		 * to userland with EAGAIN while reporting how far
		 * we've come.
		 * We wait until the socket buffer has significant free
		 * space to do bulk sends.  This makes good use of file
		 * system read ahead and allows packet segmentation
		 * offloading hardware to take over lots of work.  If
		 * we were not careful here we would send off only one
		 * sfbuf at a time.
		 */
		SOCKBUF_LOCK(&so->so_snd);
		if (so->so_snd.sb_lowat < so->so_snd.sb_hiwat / 2)
			so->so_snd.sb_lowat = so->so_snd.sb_hiwat / 2;
retry_space:
		if (so->so_snd.sb_state & SBS_CANTSENDMORE) {
			error = EPIPE;
			SOCKBUF_UNLOCK(&so->so_snd);
			goto done;
		} else if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			SOCKBUF_UNLOCK(&so->so_snd);
			goto done;
		}
		space = sbspace(&so->so_snd);
		if (space < rem &&
		    (space <= 0 ||
		     space < so->so_snd.sb_lowat)) {
			if (so->so_state & SS_NBIO) {
				SOCKBUF_UNLOCK(&so->so_snd);
				error = EAGAIN;
				goto done;
			}
			/*
			 * sbwait drops the lock while sleeping.
			 * When we loop back to retry_space the
			 * state may have changed and we retest
			 * for it.
			 */
			error = sbwait(&so->so_snd);
			/*
			 * An error from sbwait usually indicates that we've
			 * been interrupted by a signal. If we've sent anything
			 * then return bytes sent, otherwise return the error.
			 */
			if (error) {
				SOCKBUF_UNLOCK(&so->so_snd);
				goto done;
			}
			goto retry_space;
		}
		SOCKBUF_UNLOCK(&so->so_snd);

		/*
		 * Reduce space in the socket buffer by the size of
		 * the header mbuf chain.
		 * hdrlen is set to 0 after the first loop.
		 */
		space -= hdrlen;

		/*
		 * Loop and construct maximum sized mbuf chain to be bulk
		 * dumped into socket buffer.
		 */
		while (space > loopbytes) {
			vm_pindex_t pindex;
			vm_offset_t pgoff;
			struct mbuf *m0;

			VM_OBJECT_LOCK(obj);
			/*
			 * Calculate the amount to transfer.
			 * Not to exceed a page, the EOF,
			 * or the passed in nbytes.
			 */
			pgoff = (vm_offset_t)(off & PAGE_MASK);
			xfsize = omin(PAGE_SIZE - pgoff,
			    obj->un_pager.vnp.vnp_size - uap->offset -
			    fsbytes - loopbytes);
			if (uap->nbytes)
				rem = (uap->nbytes - fsbytes - loopbytes);
			else
				rem = obj->un_pager.vnp.vnp_size -
				    uap->offset - fsbytes - loopbytes;
			xfsize = omin(rem, xfsize);
			xfsize = omin(space - loopbytes, xfsize);
			if (xfsize <= 0) {
				VM_OBJECT_UNLOCK(obj);
				done = 1;		/* all data sent */
				break;
			}

			/*
			 * Attempt to look up the page.  Allocate
			 * if not found or wait and loop if busy.
			 */
			pindex = OFF_TO_IDX(off);
			pg = vm_page_grab(obj, pindex, VM_ALLOC_NOBUSY |
			    VM_ALLOC_NORMAL | VM_ALLOC_WIRED | VM_ALLOC_RETRY);

			/*
			 * Check if page is valid for what we need,
			 * otherwise initiate I/O.
			 * If we already turned some pages into mbufs,
			 * send them off before we come here again and
			 * block.
			 */
			if (pg->valid && vm_page_is_valid(pg, pgoff, xfsize))
				VM_OBJECT_UNLOCK(obj);
			else if (m != NULL)
				error = EAGAIN;	/* send what we already got */
			else if (uap->flags & SF_NODISKIO)
				error = EBUSY;
			else {
				int bsize;
				ssize_t resid;

				/*
				 * Ensure that our page is still around
				 * when the I/O completes.
				 */
				vm_page_io_start(pg);
				VM_OBJECT_UNLOCK(obj);

				/*
				 * Get the page from backing store.
				 */
				vfslocked = VFS_LOCK_GIANT(vp->v_mount);
				error = vn_lock(vp, LK_SHARED);
				if (error != 0)
					goto after_read;
				bsize = vp->v_mount->mnt_stat.f_iosize;

				/*
				 * XXXMAC: Because we don't have fp->f_cred
				 * here, we pass in NOCRED.  This is probably
				 * wrong, but is consistent with our original
				 * implementation.
				 */
				error = vn_rdwr(UIO_READ, vp, NULL, MAXBSIZE,
				    trunc_page(off), UIO_NOCOPY, IO_NODELOCKED |
				    IO_VMIO | ((MAXBSIZE / bsize) << IO_SEQSHIFT),
				    td->td_ucred, NOCRED, &resid, td);
				VOP_UNLOCK(vp, 0);
			after_read:
				VFS_UNLOCK_GIANT(vfslocked);
				VM_OBJECT_LOCK(obj);
				vm_page_io_finish(pg);
				if (!error)
					VM_OBJECT_UNLOCK(obj);
				mbstat.sf_iocnt++;
			}
			if (error) {
				vm_page_lock(pg);
				vm_page_unwire(pg, 0);
				/*
				 * See if anyone else might know about
				 * this page.  If not and it is not valid,
				 * then free it.
				 */
				if (pg->wire_count == 0 && pg->valid == 0 &&
				    pg->busy == 0 && !(pg->oflags & VPO_BUSY))
					vm_page_free(pg);
				vm_page_unlock(pg);
				VM_OBJECT_UNLOCK(obj);
				if (error == EAGAIN)
					error = 0;	/* not a real error */
				break;
			}

			/*
			 * Get a sendfile buf.  When allocating the
			 * first buffer for mbuf chain, we usually
			 * wait as long as necessary, but this wait
			 * can be interrupted.  For consequent
			 * buffers, do not sleep, since several
			 * threads might exhaust the buffers and then
			 * deadlock.
			 */
			sf = sf_buf_alloc(pg, (mnw || m != NULL) ? SFB_NOWAIT :
			    SFB_CATCH);
			if (sf == NULL) {
				mbstat.sf_allocfail++;
				vm_page_lock(pg);
				vm_page_unwire(pg, 0);
				KASSERT(pg->object != NULL,
				    ("kern_sendfile: object disappeared"));
				vm_page_unlock(pg);
				if (m == NULL)
					error = (mnw ? EAGAIN : EINTR);
				break;
			}

			/*
			 * Get an mbuf and set it up as having
			 * external storage.
			 */
			m0 = m_get((mnw ? M_NOWAIT : M_WAITOK), MT_DATA);
			if (m0 == NULL) {
				error = (mnw ? EAGAIN : ENOBUFS);
				sf_buf_mext((void *)sf_buf_kva(sf), sf);
				break;
			}
			MEXTADD(m0, sf_buf_kva(sf), PAGE_SIZE, sf_buf_mext,
			    sfs, sf, M_RDONLY, EXT_SFBUF);
			m0->m_data = (char *)sf_buf_kva(sf) + pgoff;
			m0->m_len = xfsize;

			/* Append to mbuf chain. */
			if (mtail != NULL)
				mtail->m_next = m0;
			else if (m != NULL)
				m_last(m)->m_next = m0;
			else
				m = m0;
			mtail = m0;

			/* Keep track of bits processed. */
			loopbytes += xfsize;
			off += xfsize;

			if (sfs != NULL) {
				mtx_lock(&sfs->mtx);
				sfs->count++;
				mtx_unlock(&sfs->mtx);
			}
		}

		/* Add the buffer chain to the socket buffer. */
		if (m != NULL) {
			int mlen, err;

			mlen = m_length(m, NULL);
			SOCKBUF_LOCK(&so->so_snd);
			if (so->so_snd.sb_state & SBS_CANTSENDMORE) {
				error = EPIPE;
				SOCKBUF_UNLOCK(&so->so_snd);
				goto done;
			}
			SOCKBUF_UNLOCK(&so->so_snd);
			CURVNET_SET(so->so_vnet);
			/* Avoid error aliasing. */
			err = (*so->so_proto->pr_usrreqs->pru_send)
				    (so, 0, m, NULL, NULL, td);
			CURVNET_RESTORE();
			if (err == 0) {
				/*
				 * We need two counters to get the
				 * file offset and nbytes to send
				 * right:
				 * - sbytes contains the total amount
				 *   of bytes sent, including headers.
				 * - fsbytes contains the total amount
				 *   of bytes sent from the file.
				 */
				sbytes += mlen;
				fsbytes += mlen;
				if (hdrlen) {
					fsbytes -= hdrlen;
					hdrlen = 0;
				}
			} else if (error == 0)
				error = err;
			m = NULL;	/* pru_send always consumes */
		}

		/* Quit outer loop on error or when we're done. */
		if (done) 
			break;
		if (error)
			goto done;
	}

	/*
	 * Send trailers. Wimp out and use writev(2).
	 */
	if (trl_uio != NULL) {
		sbunlock(&so->so_snd);
		error = kern_writev(td, uap->s, trl_uio);
		if (error == 0)
			sbytes += td->td_retval[0];
		goto out;
	}

done:
	sbunlock(&so->so_snd);
out:
	/*
	 * If there was no error we have to clear td->td_retval[0]
	 * because it may have been set by writev.
	 */
	if (error == 0) {
		td->td_retval[0] = 0;
	}
	if (uap->sbytes != NULL) {
		copyout(&sbytes, uap->sbytes, sizeof(off_t));
	}
	if (obj != NULL)
		vm_object_deallocate(obj);
	if (vp != NULL) {
		vfslocked = VFS_LOCK_GIANT(vp->v_mount);
		vrele(vp);
		VFS_UNLOCK_GIANT(vfslocked);
	}
	if (so)
		fdrop(sock_fp, td);
	if (m)
		m_freem(m);

	if (sfs != NULL) {
		mtx_lock(&sfs->mtx);
		if (sfs->count != 0)
			cv_wait(&sfs->cv, &sfs->mtx);
		KASSERT(sfs->count == 0, ("sendfile sync still busy"));
		cv_destroy(&sfs->cv);
		mtx_destroy(&sfs->mtx);
		free(sfs, M_TEMP);
	}

	if (error == ERESTART)
		error = EINTR;

	return (error);
}
#endif

