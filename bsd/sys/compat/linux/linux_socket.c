/*-
 * Copyright (c) 1995 Soren Schmidt
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

#include <unistd.h> /* for close() */

/* XXX we use functions that might not exist. */

#include <bsd/sys/sys/param.h>
#include <fcntl.h>
#include <osv/fcntl.h>
#include <osv/file.h>
#include <osv/uio.h>
#include <bsd/uipc_syscalls.h>

#include <bsd/sys/sys/limits.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>


#include <bsd/sys/net/if.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_systm.h>
#include <bsd/sys/netinet/ip.h>
#ifdef INET6
#include <bsd/sys/netinet/ip6.h>
#include <bsd/sys/netinet6/ip6_var.h>
#include <bsd/sys/netinet6/in6_var.h>
#endif

#include <bsd/sys/compat/linux/linux.h>
#include <bsd/sys/compat/linux/linux_socket.h>

static int linux_to_bsd_domain(int);

/*
 * Like fget() but loads the underlying socket, or returns an error if the
 * descriptor does not represent a socket.
 *
 * We bump the ref count on the returned socket.  XXX Also obtain the SX lock
 * in the future.
 *
 * Note: fgetsock() and fputsock() are deprecated, as consumers should rely
 * on their file descriptor reference to prevent the socket from being free'd
 * during use.
 */
int
fgetsock(int fd, struct socket **spp,
    u_int *fflagp)
{
	struct file *fp;
	int error;

	*spp = NULL;
	if (fflagp != NULL)
		*fflagp = 0;
	if ((error = fget(fd, &fp)) != 0)
		return (error);
	if (fp->f_type != DTYPE_SOCKET) {
		error = ENOTSOCK;
	} else {
		*spp = fp->f_data;
		if (fflagp)
			*fflagp = fp->f_flags;
		SOCK_LOCK(*spp);
		soref(*spp);
		SOCK_UNLOCK(*spp);
	}
	fdrop(fp);

	return (error);
}

/*
 * Drop the reference count on the socket and XXX release the SX lock in the
 * future.  The last reference closes the socket.
 *
 * Note: fputsock() is deprecated, see comment for fgetsock().
 */
void
fputsock(struct socket *so)
{

	ACCEPT_LOCK();
	SOCK_LOCK(so);
	CURVNET_SET(so->so_vnet);
	sorele(so);
	CURVNET_RESTORE();
}

/*
 * Reads a linux sockaddr and does any necessary translation.
 * Linux sockaddrs don't have a length field, only a family.
 * Copy the osockaddr structure pointed to by osa to kernel, adjust
 * family and convert to sockaddr.
 */
static int
linux_getsockaddr(struct sockaddr **sap, const struct osockaddr *osa, int salen)
{
	struct sockaddr *sa;
	struct osockaddr *kosa;
#ifdef INET6
	struct sockaddr_in6 *sin6;
	int oldv6size;
#endif
#if 0
	char *name;
	int bdom, error, hdrlen, namelen;
#else
	int error, bdom;
#endif
	if (salen < 2 || salen > UCHAR_MAX || !osa)
		return (EINVAL);

#ifdef INET6
	oldv6size = 0;
	/*
	 * Check for old (pre-RFC2553) sockaddr_in6. We may accept it
	 * if it's a v4-mapped address, so reserve the proper space
	 * for it.
	 */
	if (salen == sizeof(struct sockaddr_in6) - sizeof(uint32_t)) {
		salen += sizeof(uint32_t);
		oldv6size = 1;
	}
#endif

	kosa = malloc(salen);

	if ((error = copyin(osa, kosa, salen)))
		goto out;

	bdom = linux_to_bsd_domain(kosa->sa_family);
	if (bdom == -1) {
		error = EAFNOSUPPORT;
		goto out;
	}

#ifdef INET6
	/*
	 * Older Linux IPv6 code uses obsolete RFC2133 struct sockaddr_in6,
	 * which lacks the scope id compared with RFC2553 one. If we detect
	 * the situation, reject the address and write a message to system log.
	 *
	 * Still accept addresses for which the scope id is not used.
	 */
	if (oldv6size) {
		if (bdom == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)kosa;
			if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr) ||
			    (!IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_V4COMPAT(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))) {
				sin6->sin6_scope_id = 0;
			} else {
				log(LOG_DEBUG,
				    "obsolete pre-RFC2553 sockaddr_in6 rejected\n");
				error = EINVAL;
				goto out;
			}
		} else
			salen -= sizeof(uint32_t);
	}
#endif
	if (bdom == AF_INET) {
		if (salen < sizeof(struct sockaddr_in)) {
			error = EINVAL;
			goto out;
		}
		salen = sizeof(struct sockaddr_in);
	}

	/* FIXME: OSv - we don't support AD_LOCAL yet */
	assert(bdom != AF_LOCAL);
#if 0
	if (bdom == AF_LOCAL && salen > sizeof(struct sockaddr_un)) {
		hdrlen = offsetof(struct sockaddr_un, sun_path);
		name = ((struct sockaddr_un *)kosa)->sun_path;
		if (*name == '\0') {
			/*
		 	 * Linux abstract namespace starts with a NULL byte.
			 * XXX We do not support abstract namespace yet.
			 */
			namelen = strnlen(name + 1, salen - hdrlen - 1) + 1;
		} else
			namelen = strnlen(name, salen - hdrlen);
		salen = hdrlen + namelen;
		if (salen > sizeof(struct sockaddr_un)) {
			error = ENAMETOOLONG;
			goto out;
		}
	}
#endif

	sa = (struct sockaddr *)kosa;
	sa->sa_family = bdom;
	sa->sa_len = salen;

	*sap = sa;
	return (0);

out:
	free(kosa);
	return (error);
}

static int
linux_to_bsd_domain(int domain)
{

	switch (domain) {
	case LINUX_AF_UNSPEC:
		return (AF_UNSPEC);
	case LINUX_AF_UNIX:
		return (AF_LOCAL);
	case LINUX_AF_INET:
		return (AF_INET);
	case LINUX_AF_INET6:
		return (AF_INET6);
	case LINUX_AF_AX25:
		return (AF_CCITT);
	case LINUX_AF_IPX:
		return (AF_IPX);
	case LINUX_AF_APPLETALK:
		return (AF_APPLETALK);
	}
	return (-1);
}

static int
bsd_to_linux_domain(int domain)
{

	switch (domain) {
	case AF_UNSPEC:
		return (LINUX_AF_UNSPEC);
	case AF_LOCAL:
		return (LINUX_AF_UNIX);
	case AF_INET:
		return (LINUX_AF_INET);
	case AF_INET6:
		return (LINUX_AF_INET6);
	case AF_CCITT:
		return (LINUX_AF_AX25);
	case AF_IPX:
		return (LINUX_AF_IPX);
	case AF_APPLETALK:
		return (LINUX_AF_APPLETALK);
	}
	return (-1);
}

static int
linux_to_bsd_sockopt_level(int level)
{

	switch (level) {
	case LINUX_SOL_SOCKET:
		return (SOL_SOCKET);
	}
	return (level);
}

#if 0
static int
bsd_to_linux_sockopt_level(int level)
{

	switch (level) {
	case SOL_SOCKET:
		return (LINUX_SOL_SOCKET);
	}
	return (level);
}
#endif

static int
linux_to_bsd_ip_sockopt(int opt)
{

	switch (opt) {
	case LINUX_IP_TOS:
		return (IP_TOS);
	case LINUX_IP_TTL:
		return (IP_TTL);
	case LINUX_IP_OPTIONS:
		return (IP_OPTIONS);
	case LINUX_IP_MULTICAST_IF:
		return (IP_MULTICAST_IF);
	case LINUX_IP_MULTICAST_TTL:
		return (IP_MULTICAST_TTL);
	case LINUX_IP_MULTICAST_LOOP:
		return (IP_MULTICAST_LOOP);
	case LINUX_IP_ADD_MEMBERSHIP:
		return (IP_ADD_MEMBERSHIP);
	case LINUX_IP_DROP_MEMBERSHIP:
		return (IP_DROP_MEMBERSHIP);
	case LINUX_IP_HDRINCL:
		return (IP_HDRINCL);
	}
	return (-1);
}

static int
linux_to_bsd_so_sockopt(int opt)
{

	assert(opt != LINUX_SO_PEERCRED);

	switch (opt) {
	case LINUX_SO_DEBUG:
		return (SO_DEBUG);
	case LINUX_SO_REUSEADDR:
		return (SO_REUSEADDR);
	case LINUX_SO_TYPE:
		return (SO_TYPE);
	case LINUX_SO_ERROR:
		return (SO_ERROR);
	case LINUX_SO_DONTROUTE:
		return (SO_DONTROUTE);
	case LINUX_SO_BROADCAST:
		return (SO_BROADCAST);
	case LINUX_SO_SNDBUF:
		return (SO_SNDBUF);
	case LINUX_SO_RCVBUF:
		return (SO_RCVBUF);
	case LINUX_SO_KEEPALIVE:
		return (SO_KEEPALIVE);
	case LINUX_SO_OOBINLINE:
		return (SO_OOBINLINE);
	case LINUX_SO_LINGER:
		return (SO_LINGER);
	case LINUX_SO_RCVLOWAT:
		return (SO_RCVLOWAT);
	case LINUX_SO_SNDLOWAT:
		return (SO_SNDLOWAT);
	case LINUX_SO_RCVTIMEO:
		return (SO_RCVTIMEO);
	case LINUX_SO_SNDTIMEO:
		return (SO_SNDTIMEO);
	case LINUX_SO_TIMESTAMP:
		return (SO_TIMESTAMP);
	case LINUX_SO_ACCEPTCONN:
		return (SO_ACCEPTCONN);
	}
	return (-1);
}

static int
linux_to_bsd_msg_flags(int flags)
{
	int ret_flags = 0;

	if (flags & LINUX_MSG_OOB)
		ret_flags |= MSG_OOB;
	if (flags & LINUX_MSG_PEEK)
		ret_flags |= MSG_PEEK;
	if (flags & LINUX_MSG_DONTROUTE)
		ret_flags |= MSG_DONTROUTE;
	if (flags & LINUX_MSG_CTRUNC)
		ret_flags |= MSG_CTRUNC;
	if (flags & LINUX_MSG_TRUNC)
		ret_flags |= MSG_TRUNC;
	if (flags & LINUX_MSG_DONTWAIT)
		ret_flags |= MSG_DONTWAIT;
	if (flags & LINUX_MSG_EOR)
		ret_flags |= MSG_EOR;
	if (flags & LINUX_MSG_WAITALL)
		ret_flags |= MSG_WAITALL;
	if (flags & LINUX_MSG_NOSIGNAL)
		ret_flags |= MSG_NOSIGNAL;
#if 0 /* not handled */
	if (flags & LINUX_MSG_PROXY)
		;
	if (flags & LINUX_MSG_FIN)
		;
	if (flags & LINUX_MSG_SYN)
		;
	if (flags & LINUX_MSG_CONFIRM)
		;
	if (flags & LINUX_MSG_RST)
		;
	if (flags & LINUX_MSG_ERRQUEUE)
		;
#endif
	return ret_flags;
}

static int
bsd_to_linux_sockaddr(struct sockaddr *sa)
{
	if (sa == NULL)
		return EINVAL;

	u_short family = sa->sa_family;
	*(u_short *)sa = family;
	
	return (0);
}

static int
linux_to_bsd_sockaddr(struct sockaddr *sa, int len)
{
	if (sa == NULL)
		return EINVAL;

	sa_family_t family = *(sa_family_t *)sa;
	sa->sa_family = family;
	sa->sa_len = len;

	return (0);
}

static int
linux_sa_put(struct osockaddr *osa)
{
	int bdom;

	bdom = bsd_to_linux_domain(osa->sa_family);
	if (bdom == -1)
		return (EINVAL);

	osa->sa_family = bdom;

	return (0);
}

#if 0
static int
linux_to_bsd_cmsg_type(int cmsg_type)
{

	switch (cmsg_type) {
	case LINUX_SCM_RIGHTS:
		return (SCM_RIGHTS);
	case LINUX_SCM_CREDENTIALS:
		return (SCM_CREDS);
	}
	return (-1);
}

static int
bsd_to_linux_cmsg_type(int cmsg_type)
{

	switch (cmsg_type) {
	case SCM_RIGHTS:
		return (LINUX_SCM_RIGHTS);
	case SCM_CREDS:
		return (LINUX_SCM_CREDENTIALS);
	}
	return (-1);
}

#endif

static int
linux_to_bsd_msghdr(struct msghdr *hdr)
{
	/* Ignore msg_control in OSv */
	hdr->msg_control = NULL;
	hdr->msg_flags = linux_to_bsd_msg_flags(hdr->msg_flags);
	return (0);
}

static int
bsd_to_linux_msghdr(const struct msghdr *hdr)
{
	/*
	 * msg_controllen is skipped since BSD and LINUX control messages
	 * are potentially different sizes (e.g. the cred structure used
	 * by SCM_CREDS is different between the two operating system).
	 *
	 * The caller can set it (if necessary) after converting all the
	 * control messages.
	 */

	/* msg_flags skipped */
	return (0);
}

static int
linux_set_socket_flags(int s, int flags)
{
	int error;

	if (flags & LINUX_SOCK_NONBLOCK) {
		error = fcntl(s, F_SETFL, O_NONBLOCK);
		if (error)
			return (error);
	}
	if (flags & LINUX_SOCK_CLOEXEC) {
		error = fcntl(s, F_SETFD, FD_CLOEXEC);
		if (error)
			return (error);
	}
	return (0);
}

static int
linux_sendit(int s, struct msghdr *mp, int flags,
    struct mbuf *control, ssize_t *bytes)
{
	struct sockaddr *to;
	int error, bsd_flags;

	if (mp->msg_name != NULL) {
		error = linux_getsockaddr(&to, mp->msg_name, mp->msg_namelen);
		if (error)
			return (error);
		mp->msg_name = to;
	} else
		to = NULL;

	bsd_flags = linux_to_bsd_msg_flags(flags);
	error = kern_sendit(s, mp, bsd_flags, control, bytes);

	if (to)
		free(to);
	return (error);
}

/* Return 0 if IP_HDRINCL is set for the given socket. */
static int
linux_check_hdrincl(int s)
{
	int error, optval;
	socklen_t size_val;

	size_val = sizeof(optval);
	error = kern_getsockopt(s, IPPROTO_IP, IP_HDRINCL, &optval, &size_val);
	if (error)
		return (error);

	return (optval == 0);
}

/*
 * Updated sendto() when IP_HDRINCL is set:
 * tweak endian-dependent fields in the IP packet.
 */
static int
linux_sendto_hdrincl(int s, void *buf, int len, int flags, void *to,
	int tolen, ssize_t *bytes)
{
/*
 * linux_ip_copysize defines how many bytes we should copy
 * from the beginning of the IP packet before we customize it for BSD.
 * It should include all the fields we modify (ip_len and ip_off).
 */
#define linux_ip_copysize	8

	struct ip *packet;
	struct msghdr bsd_msg;
	struct iovec aiov[1];
	int error;

	/* Check that the packet isn't too big or too small. */
	if (len < linux_ip_copysize ||
	    len > IP_MAXPACKET)
		return (EINVAL);

	packet = (struct ip *)buf;

	/* Convert fields from Linux to BSD raw IP socket format */
	packet->ip_len = len;
	packet->ip_off = ntohs(packet->ip_off);

	/* Prepare the msghdr and iovec structures describing the new packet */
	bsd_msg.msg_name = PTRIN(to);
	bsd_msg.msg_namelen = tolen;
	bsd_msg.msg_iov = aiov;
	bsd_msg.msg_iovlen = 1;
	bsd_msg.msg_control = NULL;
	bsd_msg.msg_flags = 0;
	aiov[0].iov_base = (char *)packet;
	aiov[0].iov_len = len;
	error = linux_sendit(s, &bsd_msg, flags, NULL, bytes);

	return (error);
}

int
linux_socket(int domain, int type, int protocol, int *out_fd)
{
	int retval_socket, socket_flags;
	int s;

	socket_flags = type & ~LINUX_SOCK_TYPE_MASK;
	if (socket_flags & ~(LINUX_SOCK_CLOEXEC | LINUX_SOCK_NONBLOCK))
		return (EINVAL);
	type = type & LINUX_SOCK_TYPE_MASK;
	if (type < 0 || type > LINUX_SOCK_MAX)
		return (EINVAL);
	domain = linux_to_bsd_domain(domain);
	if (domain == -1)
		return (EAFNOSUPPORT);

	retval_socket = sys_socket(domain, type, protocol, &s);
	if (retval_socket)
		return (retval_socket);

	retval_socket = linux_set_socket_flags(s, socket_flags);
	if (retval_socket) {
		close(s);
		goto out;
	}

	if (type == SOCK_RAW
	    && (protocol == IPPROTO_RAW || protocol == 0)
	    && domain == PF_INET) {
		/* It's a raw IP socket: set the IP_HDRINCL option. */
		int hdrincl;

		hdrincl = 1;
		/* We ignore any error returned by kern_setsockopt() */
		kern_setsockopt(s, IPPROTO_IP, IP_HDRINCL,
		    &hdrincl, sizeof(hdrincl));
	}
#ifdef INET6
	/*
	 * Linux AF_INET6 socket has IPV6_V6ONLY setsockopt set to 0 by default
	 * and some apps depend on this. So, set V6ONLY to 0 for Linux apps.
	 * For simplicity we do this unconditionally of the net.inet6.ip6.v6only
	 * sysctl value.
	 */
	if (bsd_args.domain == PF_INET6) {
		int v6only;

		v6only = 0;
		/* We ignore any error returned by setsockopt() */
		kern_setsockopt(td, td->td_retval[0], IPPROTO_IPV6, IPV6_V6ONLY,
		    &v6only, UIO_SYSSPACE, sizeof(v6only));
	}
#endif

	/* return the file descriptor */
	*out_fd = s;
out:
	return (retval_socket);
}

int
linux_bind(int s, void *name, int namelen)
{
	struct sockaddr *sa;
	int error;

	error = linux_getsockaddr(&sa, PTRIN(name), namelen);
	if (error)
		return (error);

	error = kern_bind(s, sa);
	free(sa);
	if (error == EADDRNOTAVAIL && namelen != sizeof(struct sockaddr_in))
	   	return (EINVAL);
	return (error);
}

int
linux_connect(int s, void *name, int namelen)
{
	struct socket *so;
	struct sockaddr *sa;
	u_int fflag;
	int error;

	error = linux_getsockaddr(&sa, PTRIN(name), namelen);
	if (error)
		return (error);

	error = kern_connect(s, sa);
	free(sa);
	if (error != EISCONN)
		return (error);

	/*
	 * Linux doesn't return EISCONN the first time it occurs,
	 * when on a non-blocking socket. Instead it returns the
	 * error getsockopt(SOL_SOCKET, SO_ERROR) would return on BSD.
	 *
	 * XXXRW: Instead of using fgetsock(), check that it is a
	 * socket and use the file descriptor reference instead of
	 * creating a new one.
	 */
	error = fgetsock(s, &so, &fflag);
	if (error == 0) {
		error = EISCONN;
		if (fflag & FNONBLOCK) {
			SOCK_LOCK(so);
			if (so->so_emuldata == 0)
				error = so->so_error;
			so->so_emuldata = (void *)1;
			SOCK_UNLOCK(so);
		}
		fputsock(so);
	}
	return (error);
}

int
linux_listen(int s, int backlog)
{
	return (sys_listen(s, backlog));
}

static int
linux_accept_common(int s, struct sockaddr * name,
	socklen_t * namelen, int *out_fd, int flags)
{
	int error;

	if (flags & ~(LINUX_SOCK_CLOEXEC | LINUX_SOCK_NONBLOCK))
		return (EINVAL);

	error = sys_accept(s, name, namelen, out_fd);
	bsd_to_linux_sockaddr(name);
	if (error) {
		if (error == EFAULT && *namelen != sizeof(struct sockaddr_in))
			return (EINVAL);
		return (error);
	}

	/*
	 * linux appears not to copy flags from the parent socket to the
	 * accepted one, so we must clear the flags in the new descriptor
	 * and apply the requested flags.
	 */
	error = fcntl(*out_fd, F_SETFL, 0);
	if (error)
		goto out;
	error = linux_set_socket_flags(*out_fd, flags);
	if (error)
		goto out;
	if (name)
		error = linux_sa_put(PTRIN(name));

out:
	if (error) {
		close(*out_fd);
		*out_fd = 0;
	}
	return (error);
}

int linux_accept(int s, struct sockaddr * name,
	socklen_t * namelen, int *out_fd)
{

	return (linux_accept_common(s, name, namelen, out_fd, 0));
}

int
linux_accept4(int s, struct sockaddr * name,
	socklen_t * namelen, int *out_fd, int flags)
{

	return (linux_accept_common(s, name, namelen, out_fd, flags));
}

/* FIXME: OSv - few functions have not been ported */
#if 0
struct linux_getsockname_args {
	int s;
	l_uintptr_t addr;
	l_uintptr_t namelen;
};

static int
linux_getsockname(struct thread *td, struct linux_getsockname_args *args)
{
	struct getsockname_args /* {
		int	fdes;
		struct sockaddr * __restrict asa;
		socklen_t * __restrict alen;
	} */ bsd_args;
	int error;

	bsd_args.fdes = args->s;
	/* XXX: */
	bsd_args.asa = (struct sockaddr * __restrict)PTRIN(args->addr);
	bsd_args.alen = PTRIN(args->namelen);	/* XXX */
	error = sys_getsockname(td, &bsd_args);
	bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.asa);
	if (error)
		return (error);
	error = linux_sa_put(PTRIN(args->addr));
	if (error)
		return (error);
	return (0);
}

struct linux_getpeername_args {
	int s;
	l_uintptr_t addr;
	l_uintptr_t namelen;
};

static int
linux_getpeername(struct thread *td, struct linux_getpeername_args *args)
{
	struct getpeername_args /* {
		int fdes;
		caddr_t asa;
		int *alen;
	} */ bsd_args;
	int error;

	bsd_args.fdes = args->s;
	bsd_args.asa = (struct sockaddr *)PTRIN(args->addr);
	bsd_args.alen = (int *)PTRIN(args->namelen);
	error = sys_getpeername(td, &bsd_args);
	bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.asa);
	if (error)
		return (error);
	error = linux_sa_put(PTRIN(args->addr));
	if (error)
		return (error);
	return (0);
}

#endif

int
linux_socketpair(int domain, int type, int protocol, int* rsv)
{
	int error, socket_flags;

	domain = linux_to_bsd_domain(domain);
	if (domain != PF_LOCAL)
		return (EAFNOSUPPORT);

	socket_flags = type & ~LINUX_SOCK_TYPE_MASK;
	if (socket_flags & ~(LINUX_SOCK_CLOEXEC | LINUX_SOCK_NONBLOCK))
		return (EINVAL);
	type = type & LINUX_SOCK_TYPE_MASK;
	if (type < 0 || type > LINUX_SOCK_MAX)
		return (EINVAL);

	assert(protocol != PF_UNIX);

	if (protocol != 0 && protocol != PF_UNIX)

		/*
		 * Use of PF_UNIX as protocol argument is not right,
		 * but Linux does it.
		 * Do not map PF_UNIX as its Linux value is identical
		 * to FreeBSD one.
		 */
		return (EPROTONOSUPPORT);
	else
		protocol = 0;
	error = kern_socketpair(domain, type, protocol, rsv);
	if (error)
		return (error);
	error = linux_set_socket_flags(rsv[0], socket_flags);
	if (error)
		goto out;
	error = linux_set_socket_flags(rsv[1], socket_flags);
	if (error)
		goto out;

out:
	if (error) {
		(void)close(rsv[0]);
		(void)close(rsv[1]);
	}
	return (error);
}

int
linux_send(int s, caddr_t buf, size_t len, int flags, ssize_t* bytes)
{
	int bsd_flags = linux_to_bsd_msg_flags(flags);
	return sys_sendto(s, buf, len, bsd_flags, NULL, 0, bytes);
}

int
linux_recv(int s, caddr_t buf, int len, int flags, ssize_t* bytes)
{
	int bsd_flags = linux_to_bsd_msg_flags(flags);
	return (sys_recvfrom(s, buf, len, bsd_flags, NULL, 0, bytes));
}

int
linux_sendto(int s, void* buf, int len, int flags,
	void* to, int tolen, ssize_t *bytes)
{
	struct msghdr msg;
	struct iovec aiov;
	int error;

	if (linux_check_hdrincl(s) == 0)
		/* IP_HDRINCL set, tweak the packet before sending */
		return (linux_sendto_hdrincl(s, buf, len, flags, to, tolen, bytes));

	msg.msg_name = PTRIN(to);
	msg.msg_namelen = tolen;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = 0;
	aiov.iov_base = PTRIN(buf);
	aiov.iov_len = len;
	error = linux_sendit(s, &msg, flags, NULL, bytes);
	return (error);
}

int
linux_recvfrom(int s, void* buf, size_t len, int flags,
	struct sockaddr * from, socklen_t * fromlen, ssize_t* bytes)
{
	int error;

	int bsd_flags = linux_to_bsd_msg_flags(flags);
	linux_to_bsd_sockaddr(from, len);
	error = sys_recvfrom(s, (caddr_t)buf, len, bsd_flags, from,
		fromlen, bytes);
	bsd_to_linux_sockaddr(from);
	
	if (error)
		return (error);
	if (from) {
		error = linux_sa_put((struct osockaddr *)from);
		if (error)
			return (error);
	}
	return (0);
}

int
linux_sendmsg(int s, struct msghdr* msg, int flags, ssize_t* bytes)
{
#if 0
	struct cmsghdr *cmsg;
	struct mbuf *control;
	struct iovec *iov;
	socklen_t datalen;
	struct sockaddr *sa;
	sa_family_t sa_family;
	void *data;
#endif

	int error;

	/*
	 * Some Linux applications (ping) define a non-NULL control data
	 * pointer, but a msg_controllen of 0, which is not allowed in the
	 * FreeBSD system call interface.  NULL the msg_control pointer in
	 * order to handle this case.  This should be checked, but allows the
	 * Linux ping to work.
	 */
	if (msg->msg_control != NULL && msg->msg_controllen == 0)
		msg->msg_control = NULL;

	/* FIXME: Translate msg control */
	assert(msg->msg_control == NULL);

	error = linux_to_bsd_msghdr(msg);
	if (error)
		return (error);

	/* FIXME: OSv - cmsgs translation is done credentials and rights,
	   we ignore those in OSv. */
#if 0
	if ((ptr_cmsg = LINUX_CMSG_FIRSTHDR(&linux_msg)) != NULL) {
		error = kern_getsockname(td, args->s, &sa, &datalen);
		if (error)
			goto bad;
		sa_family = sa->sa_family;
		free(sa, M_SONAME);

		error = ENOBUFS;
		cmsg = malloc(CMSG_HDRSZ, M_TEMP, M_WAITOK | M_ZERO);
		control = m_get(M_WAIT, MT_CONTROL);
		if (control == NULL)
			goto bad;

		do {
			error = copyin(ptr_cmsg, &linux_cmsg,
			    sizeof(struct l_cmsghdr));
			if (error)
				goto bad;

			error = EINVAL;
			if (linux_cmsg.cmsg_len < sizeof(struct l_cmsghdr))
				goto bad;

			/*
			 * Now we support only SCM_RIGHTS and SCM_CRED,
			 * so return EINVAL in any other cmsg_type
			 */
			cmsg->cmsg_type =
			    linux_to_bsd_cmsg_type(linux_cmsg.cmsg_type);
			cmsg->cmsg_level =
			    linux_to_bsd_sockopt_level(linux_cmsg.cmsg_level);
			if (cmsg->cmsg_type linux_sendmsg== -1
			    || cmsg->cmsg_level != SOL_SOCKET)
				goto bad;

			/*
			 * Some applications (e.g. pulseaudio) attempt to
			 * send ancillary data even if the underlying protocol
			 * doesn't support it which is not allowed in the
			 * FreeBSD system call interface.
			 */
			if (sa_family != AF_UNIX)
				continue;

			data = LINUX_CMSG_DATA(ptr_cmsg);
			datalen = linux_cmsg.cmsg_len - L_CMSG_HDRSZ;

			switch (cmsg->cmsg_type)
			{
			case SCM_RIGHTS:
				break;

			case SCM_CREDS:
				data = &cmcred;
				datalen = sizeof(cmcred);

				/*
				 * The lower levels will fill in the structure
				 */
				bzero(data, datalen);
				break;
			}

			cmsg->cmsg_len = CMSG_LEN(datalen);

			error = ENOBUFS;
			if (!m_append(control, CMSG_HDRSZ, (c_caddr_t)cmsg))
				goto bad;
			if (!m_append(control, datalen, (c_caddr_t)data))
				goto bad;
		} while ((ptr_cmsg = LINUX_CMSG_NXTHDR(&linux_msg, ptr_cmsg)));

		if (m_length(control, NULL) == 0) {
			m_freem(control);
			control = NULL;
		}
	}
#endif

	error = linux_sendit(s, msg, flags, NULL, bytes);

#if 0
bad:
	free(iov);
	if (cmsg)
		free(cmsg);
#endif
	return (error);
}

struct linux_recvmsg_args {
	int s;
	l_uintptr_t msg;
	int flags;
};

/* FIXME: OSv - flags are ignored, the flags
 * inside the msghdr are used instead */
int
linux_recvmsg(int s, struct msghdr *msg, int flags, ssize_t* bytes)
{
#if 0
	socklen_t datalen, outlen;
	struct mbuf *control = NULL;
	struct mbuf **controlp;
	caddr_t outbuf;
	void *data;
	int error, i, fd, fds, *fdp;
#endif
	int error;
	error = linux_to_bsd_msghdr(msg);
	if (error)
		return (error);

	if (msg->msg_name) {
		error = linux_to_bsd_sockaddr((struct sockaddr *)msg->msg_name,
		    msg->msg_namelen);
		if (error)
			goto bad;
	}

	assert(msg->msg_control == NULL);

	error = kern_recvit(s, msg, NULL, bytes);
	if (error)
		goto bad;

	error = bsd_to_linux_msghdr(msg);
	if (error)
		goto bad;

	if (msg->msg_name) {
		error = bsd_to_linux_sockaddr((struct sockaddr *)msg->msg_name);
		if (error)
			goto bad;
	}
	if (msg->msg_name && msg->msg_namelen > 2) {
		error = linux_sa_put(msg->msg_name);
		if (error)
			goto bad;
	}

	assert(msg->msg_controllen == 0);
	assert(msg->msg_control == NULL);

#if 0
	if (control) {
		linux_cmsg = malloc(L_CMSG_HDRSZ, M_TEMP, M_WAITOK | M_ZERO);

		msg.msg_control = mtod(control, struct cmsghdr *);
		msg.msg_controllen = control->m_len;

		cm = CMSG_FIRSTHDR(&msg);

		while (cm != NULL) {
			linux_cmsg->cmsg_type =
			    bsd_to_linux_cmsg_type(cm->cmsg_type);
			linux_cmsg->cmsg_level =
			    bsd_to_linux_sockopt_level(cm->cmsg_level);
			if (linux_cmsg->cmsg_type == -1
			    || cm->cmsg_level != SOL_SOCKET)
			{
				error = EINVAL;
				goto bad;
			}

			data = CMSG_DATA(cm);
			datalen = (caddr_t)cm + cm->cmsg_len - (caddr_t)data;

			switch (cm->cmsg_type)
			{
			case SCM_RIGHTS:
				if (args->flags & LINUX_MSG_CMSG_CLOEXEC) {
					fds = datalen / sizeof(int);
					fdp = data;
					for (i = 0; i < fds; i++) {
						fd = *fdp++;
						(void)kern_fcntl(td, fd,
						    F_SETFD, FD_CLOEXEC);
					}
				}
				break;

			case SCM_CREDS:
				/*
				 * Currently LOCAL_CREDS is never in
				 * effect for Linux so no need to worry
				 * about sockcred
				 */
				if (datalen != sizeof(*cmcred)) {
					error = EMSGSIZE;
					goto bad;
				}
				cmcred = (struct cmsgcred *)data;
				bzero(&linux_ucred, sizeof(linux_ucred));
				linux_ucred.pid = cmcred->cmcred_pid;
				linux_ucred.uid = cmcred->cmcred_uid;
				linux_ucred.gid = cmcred->cmcred_gid;
				data = &linux_ucred;
				datalen = sizeof(linux_ucred);
				break;
			}

			if (outlen + LINUX_CMSG_LEN(datalen) >
			    linux_msg.msg_controllen) {
				if (outlen == 0) {
					error = EMSGSIZE;
					goto bad;
				} else {
					linux_msg.msg_flags |=
					    LINUX_MSG_CTRUNC;
					goto out;
				}
			}

			linux_cmsg->cmsg_len = LINUX_CMSG_LEN(datalen);

			error = copyout(linux_cmsg, outbuf, L_CMSG_HDRSZ);
			if (error)
				goto bad;
			outbuf += L_CMSG_HDRSZ;

			error = copyout(data, outbuf, datalen);
			if (error)
				goto bad;

			outbuf += LINUX_CMSG_ALIGN(datalen);
			outlen += LINUX_CMSG_LEN(datalen);

			cm = CMSG_NXTHDR(&msg, cm);
		}
	}

out:
	linux_msg.msg_controllen = outlen;
	error = copyout(&linux_msg, PTRIN(args->msg), sizeof(linux_msg));

bad:
	free(iov);
	if (control != NULL)
		m_freem(control);
	if (linux_cmsg != NULL)
		free(linux_cmsg);
#endif

bad:

	return (error);
}

int
linux_shutdown(int s, int how)
{
	return (sys_shutdown(s, how));
}

int
linux_setsockopt(int s, int level, int name, caddr_t val, int valsize)
{
	int error;

	level = linux_to_bsd_sockopt_level(level);
	switch (level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(name);
		break;
	case IPPROTO_IP:
		name = linux_to_bsd_ip_sockopt(name);
		break;
	case IPPROTO_TCP:
		/* Linux TCP option values match BSD's */
		break;
	default:
		name = -1;
		break;
	}
	if (name == -1)
		return (ENOPROTOOPT);

	/* FIXME: OSv - enable when we have IPv6 */
#if 0
	if (name == IPV6_NEXTHOP) {
		linux_to_bsd_sockaddr((struct sockaddr *)bsd_args.val,
			bsd_args.valsize);
		error = sys_setsockopt(td, &bsd_args);
		bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.val);
	} else
#endif
		error = sys_setsockopt(s, level, name, val, valsize);

	return (error);
}

int
linux_getsockopt(int s, int level, int name, void *val, socklen_t *valsize)
{
	int error;

	level = linux_to_bsd_sockopt_level(level);
	switch (level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(name);
		break;
	case IPPROTO_IP:
		name = linux_to_bsd_ip_sockopt(name);
		break;
	case IPPROTO_TCP:
		/* Linux TCP option values match BSD's */
		break;
	default:
		name = -1;
		break;
	}
	if (name == -1)
		return (EINVAL);

	/* FIXME: OSv - enable when we have IPv6 */
#if 0
	if (name == IPV6_NEXTHOP) {
		error = sys_getsockopt(td, &bsd_args);
		bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.val);
	} else
#endif
		error = sys_getsockopt(s, level, name, val, valsize);

	return (error);
}
