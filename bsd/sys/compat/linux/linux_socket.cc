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
#include <bsd/sys/sys/malloc.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>


#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_dl.h>
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
#include <bsd/sys/compat/linux/linux_netlink.h>

#define __NEED_sa_family_t
#include <bits/alltypes.h>


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
	if (file_type(fp) != DTYPE_SOCKET) {
		error = ENOTSOCK;
	} else {
		*spp = (socket*)file_data(fp);
		if (fflagp)
			*fflagp = file_flags(fp);
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
 * Reads a linux bsd_sockaddr and does any necessary translation.
 * Linux bsd_sockaddrs don't have a length field, only a family.
 * Copy the bsd_osockaddr structure pointed to by osa to kernel, adjust
 * family and convert to bsd_sockaddr.
 */
static int
linux_getsockaddr(struct bsd_sockaddr **sap, const struct bsd_osockaddr *osa, int salen)
{
	struct bsd_sockaddr *sa;
	struct bsd_osockaddr *kosa;
#ifdef INET6
	struct bsd_sockaddr_in6 *sin6;
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
	 * Check for old (pre-RFC2553) bsd_sockaddr_in6. We may accept it
	 * if it's a v4-mapped address, so reserve the proper space
	 * for it.
	 */
	if (salen == sizeof(struct bsd_sockaddr_in6) - sizeof(uint32_t)) {
		salen += sizeof(uint32_t);
		oldv6size = 1;
	}
#endif

	kosa = (bsd_osockaddr*)malloc(salen);

	if ((error = copyin(osa, kosa, salen)))
		goto out;

	bdom = linux_to_bsd_domain(kosa->sa_family);
	if (bdom == -1) {
		error = EAFNOSUPPORT;
		goto out;
	}

#ifdef INET6
	/*
	 * Older Linux IPv6 code uses obsolete RFC2133 struct bsd_sockaddr_in6,
	 * which lacks the scope id compared with RFC2553 one. If we detect
	 * the situation, reject the address and write a message to system log.
	 *
	 * Still accept addresses for which the scope id is not used.
	 */
	if (oldv6size) {
		if (bdom == AF_INET6) {
			sin6 = (struct bsd_sockaddr_in6 *)kosa;
			if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr) ||
			    (!IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_V4COMPAT(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))) {
				sin6->sin6_scope_id = 0;
			} else {
				bsd_log(LOG_DEBUG,
					"obsolete pre-RFC2553 bsd_sockaddr_in6 rejected\n");
				error = EINVAL;
				goto out;
			}
		} else
			salen -= sizeof(uint32_t);
	}
#endif
	if (bdom == AF_INET) {
		if ((size_t)salen < sizeof(struct bsd_sockaddr_in)) {
			error = EINVAL;
			goto out;
		}
		salen = sizeof(struct bsd_sockaddr_in);
	}

	/* FIXME: OSv - we don't support AD_LOCAL yet */
	assert(bdom != AF_LOCAL);
#if 0
	if (bdom == AF_LOCAL && salen > sizeof(struct bsd_sockaddr_un)) {
		hdrlen = offsetof(struct bsd_sockaddr_un, sun_path);
		name = ((struct bsd_sockaddr_un *)kosa)->sun_path;
		if (*name == '\0') {
			/*
		 	 * Linux abstract namespace starts with a NULL byte.
			 * XXX We do not support abstract namespace yet.
			 */
			namelen = strnlen(name + 1, salen - hdrlen - 1) + 1;
		} else
			namelen = strnlen(name, salen - hdrlen);
		salen = hdrlen + namelen;
		if (salen > sizeof(struct bsd_sockaddr_un)) {
			error = ENAMETOOLONG;
			goto out;
		}
	}
#endif

	sa = (struct bsd_sockaddr *)kosa;
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
	case LINUX_AF_NETLINK:
		return (AF_NETLINK);
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
	case AF_NETLINK:
		return (LINUX_AF_NETLINK);
	}
	return (-1);
}

static int
bsd_to_linux_sockopt_level(int level)
{
	switch (level) {
	case SOL_SOCKET:
		return (LINUX_SOL_SOCKET);
	}
	return (level);
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

#ifdef INET6

static int
linux_to_bsd_ipv6_sockopt(int opt)
{

	switch (opt) {
	case LINUX_IPV6_ADDRFORM:
		return (IPV6_ADDRFORM);
	case LINUX_IPV6_2292PKTINFO:
		return (IPV6_2292PKTINFO);
	case LINUX_IPV6_2292HOPOPTS:
		return (IPV6_2292HOPOPTS);
	case LINUX_IPV6_2292DSTOPTS:
		return (IPV6_2292DSTOPTS);
	case LINUX_IPV6_2292RTHDR:
		return (IPV6_2292RTHDR);
	case LINUX_IPV6_2292PKTOPTIONS:
		return (IPV6_2292PKTOPTIONS);
	case LINUX_IPV6_CHECKSUM:
		return (IPV6_CHECKSUM);
	case LINUX_IPV6_2292HOPLIMIT:
		return (IPV6_2292HOPLIMIT);
#ifdef LINUX_IPV6_RXSRCRT
	case LINUX_IPV6_RXSRCRT:
		return (IPV6_RXSRCRT);
#endif
	case LINUX_IPV6_NEXTHOP:
		return (IPV6_NEXTHOP);
	case LINUX_IPV6_AUTHHDR:
		return (IPV6_AUTHHDR);
	case LINUX_IPV6_UNICAST_HOPS:
		return (IPV6_UNICAST_HOPS);
	case LINUX_IPV6_MULTICAST_IF:
		return (IPV6_MULTICAST_IF);
	case LINUX_IPV6_MULTICAST_HOPS:
		return (IPV6_MULTICAST_HOPS);
	case LINUX_IPV6_MULTICAST_LOOP:
		return (IPV6_MULTICAST_LOOP);
	case LINUX_IPV6_JOIN_GROUP:
		return (IPV6_JOIN_GROUP);
	case LINUX_IPV6_LEAVE_GROUP:
		return (IPV6_LEAVE_GROUP);
	case LINUX_IPV6_ROUTER_ALERT:
		return (IPV6_ROUTER_ALERT);
	case LINUX_IPV6_MTU_DISCOVER:
		return (IPV6_MTU_DISCOVER);
	case LINUX_IPV6_MTU:
		return (IPV6_MTU);
	case LINUX_IPV6_RECVERR:
		return (IPV6_RECVERR);
	case LINUX_IPV6_V6ONLY:
		return (IPV6_V6ONLY);
	case LINUX_IPV6_JOIN_ANYCAST:
		return (IPV6_JOIN_ANYCAST);
	case LINUX_IPV6_LEAVE_ANYCAST:
		return (IPV6_LEAVE_ANYCAST);
	case LINUX_IPV6_IPSEC_POLICY:
		return (IPV6_IPSEC_POLICY);
	case LINUX_IPV6_XFRM_POLICY:
		return (IPV6_XFRM_POLICY);
	case LINUX_IPV6_RECVPKTINFO:
		return (IPV6_RECVPKTINFO);
	case LINUX_IPV6_PKTINFO:
		return (IPV6_PKTINFO);
	case LINUX_IPV6_RECVHOPLIMIT:
		return (IPV6_RECVHOPLIMIT);
	case LINUX_IPV6_HOPLIMIT:
		return (IPV6_HOPLIMIT);
	case LINUX_IPV6_RECVHOPOPTS:
		return (IPV6_RECVHOPOPTS);
	case LINUX_IPV6_HOPOPTS:
		return (IPV6_HOPOPTS);
	case LINUX_IPV6_RTHDRDSTOPTS:
		return (IPV6_RTHDRDSTOPTS);
	case LINUX_IPV6_RECVRTHDR:
		return (IPV6_RECVRTHDR);
	case LINUX_IPV6_RTHDR:
		return (IPV6_RTHDR);
	case LINUX_IPV6_RECVDSTOPTS:
		return (IPV6_RECVDSTOPTS);
	case LINUX_IPV6_DSTOPTS:
		return (IPV6_DSTOPTS);
	case LINUX_IPV6_RECVPATHMTU:
		return (IPV6_RECVPATHMTU);
	case LINUX_IPV6_PATHMTU:
		return (IPV6_PATHMTU);
	case LINUX_IPV6_DONTFRAG:
		return (IPV6_DONTFRAG);
	case LINUX_IPV6_RECVTCLASS:
		return (IPV6_RECVTCLASS);
	case LINUX_IPV6_TCLASS:
		return (IPV6_TCLASS);
	}
	return (-1);
}

#endif // INET6


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
	case LINUX_SO_REUSEPORT:
		return (SO_REUSEPORT);
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
bsd_to_linux_sockaddr(struct bsd_sockaddr *sa)
{
	if (sa == NULL)
		return EINVAL;

	u_short family = sa->sa_family;
	*(u_short *)sa = family;

	return (0);
}

static int
linux_to_bsd_sockaddr(struct bsd_sockaddr *sa, int len)
{
	if (sa == NULL)
		return EINVAL;

	sa_family_t family = *(sa_family_t *)sa;
	sa->sa_family = family;
	sa->sa_len = len;

	return (0);
}

static int
linux_sa_put(struct bsd_osockaddr *osa)
{
	int bdom;

	bdom = bsd_to_linux_domain(osa->sa_family);
	if (bdom == -1)
		return (EINVAL);

	osa->sa_family = bdom;

	return (0);
}

static int
linux_to_bsd_cmsg_type(int cmsg_level, int cmsg_type)
{
	switch(cmsg_level) {
	case LINUX_SOL_SOCKET:
		switch (cmsg_type) {
#if 0
		case LINUX_SCM_RIGHTS:
			return (SCM_RIGHTS);
		case LINUX_SCM_CREDENTIALS:
			return (SCM_CREDS);
#endif
		case LINUX_SCM_TIMESTAMP:
			return (SCM_TIMESTAMP);
		}
		break;
	case IPPROTO_IP:
		switch (cmsg_type) {
		case IP_PKTINFO:
			return cmsg_type;
		}
		break;
#ifdef INET6
	case IPPROTO_IPV6:
		switch (cmsg_type) {
		case IPV6_PKTINFO:
			return cmsg_type;
		}
		break;
#endif
	}
	return (-1);
}

static int
bsd_to_linux_cmsg_type(int cmsg_level, int cmsg_type)
{

	switch (cmsg_level) {
	case SOL_SOCKET:
		switch (cmsg_type) {
#if 0
		case SCM_RIGHTS:
			return (LINUX_SCM_RIGHTS);
		case SCM_CREDS:
			return (LINUX_SCM_CREDENTIALS);
#endif
		case SCM_TIMESTAMP:
			return (LINUX_SCM_TIMESTAMP);
		}
		break;
	case IPPROTO_IP:
		switch (cmsg_type) {
		case IP_RECVIF:
		case IP_RECVDSTADDR:
			// IP_RECVIF and IP_RECVDSTADDR get combined into IP_PKTINFO
			return IP_PKTINFO;
		case IP_PKTINFO:
			return cmsg_type;
		}
		break;
#ifdef INET6
	case IPPROTO_IPV6:
		switch (cmsg_type) {
		case IPV6_PKTINFO:
			return cmsg_type;
		}
		break;
#endif
	}

	return (-1);
}

static int
linux_to_bsd_msghdr(struct msghdr *bhdr, const struct l_msghdr *lhdr)
{
	if (lhdr->msg_controllen > INT_MAX)
		return (ENOBUFS);

	bhdr->msg_name		= (void *)lhdr->msg_name;
	bhdr->msg_namelen = lhdr->msg_namelen;
	bhdr->msg_iov			= (iovec *)lhdr->msg_iov;
	bhdr->msg_iovlen	= lhdr->msg_iovlen;
	bhdr->msg_control = (void *)lhdr->msg_control;

	/*
	 * msg_controllen is skipped since BSD and LINUX control messages
	 * are potentially different sizes (e.g. the cred structure used
	 * by SCM_CREDS is different between the two operating system).
	 *
	 * The caller can set it (if necessary) after converting all the
	 * control messages.
	 */

	bhdr->msg_flags = linux_to_bsd_msg_flags(lhdr->msg_flags);
	return (0);
}

static int
bsd_to_linux_msghdr(const struct msghdr *bhdr, struct l_msghdr *lhdr)
{
	lhdr->msg_name		= (l_uintptr_t)bhdr->msg_name;
	lhdr->msg_namelen = bhdr->msg_namelen;
	lhdr->msg_iov			= (l_uintptr_t)bhdr->msg_iov;
	lhdr->msg_iovlen	= bhdr->msg_iovlen;
	lhdr->msg_control = (l_uintptr_t)bhdr->msg_control;

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
	struct bsd_sockaddr *to;
	int error, bsd_flags;

	if (mp->msg_name != NULL) {
		error = linux_getsockaddr(&to, (const bsd_osockaddr*)mp->msg_name, mp->msg_namelen);
		if (error) {
			if (control)
				m_freem(control);
			return (error);
		}
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
	    len > IP_MAXSEGMENT)
		return (EINVAL);

	packet = (struct ip *)buf;

	/* Convert fields from Linux to BSD raw IP socket format */
	packet->ip_len = len;
	packet->ip_off = ntohs(packet->ip_off);

	/* Prepare the msghdr and iovec structures describing the new packet */
	bsd_msg.msg_name = to;
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
	if (domain == PF_INET6) {
		int v6only;

		v6only = 0;
		/* We ignore any error returned by setsockopt() */
		kern_setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
		    &v6only, sizeof(v6only));
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
	struct bsd_sockaddr *sa;
	int error;

	error = linux_getsockaddr(&sa, (const bsd_osockaddr*)name, namelen);
	if (error)
		return (error);

	error = kern_bind(s, sa);
	free(sa);
	if (error == EADDRNOTAVAIL && namelen != sizeof(struct bsd_sockaddr_in))
	   	return (EINVAL);
	return (error);
}

int
linux_connect(int s, void *name, int namelen)
{
	struct socket *so;
	struct bsd_sockaddr *sa;
	u_int fflag;
	int error;

	error = linux_getsockaddr(&sa, (const bsd_osockaddr*)name, namelen);
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
linux_accept_common(int s, struct bsd_sockaddr * name,
	socklen_t * namelen, int *out_fd, int flags)
{
	int error;

	if (flags & ~(LINUX_SOCK_CLOEXEC | LINUX_SOCK_NONBLOCK))
		return (EINVAL);

	error = sys_accept(s, name, namelen, out_fd);
	bsd_to_linux_sockaddr(name);
	if (error) {
		if (error == EFAULT && *namelen != sizeof(struct bsd_sockaddr_in))
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
		error = linux_sa_put((struct bsd_osockaddr *)name);

out:
	if (error) {
		close(*out_fd);
		*out_fd = 0;
	}
	return (error);
}

int linux_accept(int s, struct bsd_sockaddr * name,
	socklen_t * namelen, int *out_fd)
{

	return (linux_accept_common(s, name, namelen, out_fd, 0));
}

int
linux_accept4(int s, struct bsd_sockaddr * name,
	socklen_t * namelen, int *out_fd, int flags)
{

	return (linux_accept_common(s, name, namelen, out_fd, flags));
}

int
linux_getsockname(int s, struct bsd_sockaddr *addr, socklen_t *addrlen)
{
	int error;

	error = sys_getsockname(s, addr, addrlen);
	bsd_to_linux_sockaddr(addr);
	if (error)
		return (error);
	error = linux_sa_put((struct bsd_osockaddr *)addr);
	if (error)
		return (error);
	return (0);
}

int
linux_getpeername(int s, struct bsd_sockaddr *addr, socklen_t *namelen)
{
	int error;

	error = sys_getpeername(s, addr, namelen);
	bsd_to_linux_sockaddr(addr);
	if (error)
		return (error);
	error = linux_sa_put((struct bsd_osockaddr *)addr);
	if (error)
		return (error);
	return (0);
}

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

	msg.msg_name = to;
	msg.msg_namelen = tolen;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = 0;
	aiov.iov_base = buf;
	aiov.iov_len = len;
	error = linux_sendit(s, &msg, flags, NULL, bytes);
	return (error);
}

int
linux_recvfrom(int s, void* buf, size_t len, int flags,
	struct bsd_sockaddr * from, socklen_t * fromlen, ssize_t* bytes)
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
		error = linux_sa_put((struct bsd_osockaddr *)from);
		if (error)
			return (error);
	}
	return (0);
}

int
linux_sendmsg(int s, struct l_msghdr* linux_msg, int flags, ssize_t* bytes)
{
#if 0
	sa_family_t sa_family;
	struct bsd_sockaddr *sa;
	struct iovec *iov;
#endif
	struct l_cmsghdr *linux_cmsg;
	struct cmsghdr *cmsg;
	struct mbuf *control = 0;
	socklen_t datalen;
	void *data;

	struct msghdr msg;
	int error;

	/*
	 * Some Linux applications (ping) define a non-NULL control data
	 * pointer, but a msg_controllen of 0, which is not allowed in the
	 * FreeBSD system call interface.  NULL the msg_control pointer in
	 * order to handle this case.  This should be checked, but allows the
	 * Linux ping to work.
	 */

	if (linux_msg->msg_control != NULL && linux_msg->msg_controllen == 0)
		linux_msg->msg_control = NULL;

	error = linux_to_bsd_msghdr(&msg, linux_msg);
	if (error)
		return (error);

	/* Linux to BSD cmsgs translation */
	if ((linux_cmsg = LINUX_CMSG_FIRSTHDR(linux_msg)) != NULL) {
		uint8_t cmsg_buf[CMSG_HDRSZ];

#if 0
		// TODO: This causes unnecessary malloc/free
		//       Does this really need to be done?
		error = kern_getsockname(td, args->s, &sa, &datalen);
		if (error)
			goto bad;
		sa_family = sa->sa_family;
		free(sa);
#endif

		cmsg = (struct cmsghdr*) cmsg_buf;
		error = ENOBUFS;
		control = m_get(M_WAIT, MT_CONTROL);
		if (control == NULL)
			goto bad;

		do {
			error = EINVAL;
			if (linux_cmsg->cmsg_len < sizeof(struct l_cmsghdr))
				goto bad;

			cmsg->cmsg_type =
				linux_to_bsd_cmsg_type(linux_cmsg->cmsg_level, linux_cmsg->cmsg_type);
			cmsg->cmsg_level =
				linux_to_bsd_sockopt_level(linux_cmsg->cmsg_level);
			if (cmsg->cmsg_type == -1)
				goto bad;

			data = LINUX_CMSG_DATA(linux_cmsg);
			datalen = linux_cmsg->cmsg_len - L_CMSG_HDRSZ;

			if (cmsg->cmsg_level == SOL_SOCKET) {
#if 0
				/*
				 * Some applications (e.g. pulseaudio) attempt to
				 * send ancillary data even if the underlying protocol
				 * doesn't support it which is not allowed in the
				 * FreeBSD system call interface.
				 */
				if (sa_family != AF_UNIX)
					continue;

				switch (cmsg->cmsg_type) {
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
				default:
					goto bad;
				}
#else
				goto bad;
#endif
			}
			else if (cmsg->cmsg_level == IPPROTO_IP) {
				switch(cmsg->cmsg_type) {
				case IP_PKTINFO:
					break;
				default:
					goto bad;
				}
			}
#ifdef INET6
			else if (cmsg->cmsg_level == IPPROTO_IPV6) {
				switch(cmsg->cmsg_type) {
				case IPV6_PKTINFO:
					break;
				default:
					goto bad;
				}
			}
#endif /* INET6 */
			else {
				goto bad;
			}

			cmsg->cmsg_len = CMSG_LEN(datalen);

			error = ENOBUFS;
			if (!m_append(control, CMSG_HDRSZ, (c_caddr_t)cmsg) ||
			    !m_append(control, datalen, (c_caddr_t)data)) {
				goto bad;
			}

		} while ((linux_cmsg = LINUX_CMSG_NXTHDR(linux_msg, linux_cmsg)));

		if (m_length(control, NULL) == 0) {
			m_freem(control);
			control = NULL;
		}
	}

	error = linux_sendit(s, &msg, flags, control, bytes);
	control = NULL; /* transfered ownership of control mbuf */
bad:
#if 0
	free(iov);
	if (cmsg)
		free(cmsg);
#endif
	if (control)
		m_freem(control);
	return (error);
}

struct linux_recvmsg_args {
	int s;
	l_uintptr_t msg;
	int flags;
};

int
linux_recvmsg_append_cmsg(caddr_t buf, socklen_t buflen,
						  struct l_cmsghdr *linux_cmsg, caddr_t data, socklen_t datalen)
{
	int error;
	caddr_t dst = buf;

	if (LINUX_CMSG_LEN(datalen) > buflen)
		return EMSGSIZE;

	error = copyout(linux_cmsg, dst, L_CMSG_HDRSZ);
	if (error)
		return error;
	dst += L_CMSG_HDRSZ;

	error = copyout(data, dst, datalen);
	if (error)
		return error; 

	return 0;
}

int
linux_recvmsg(int s, struct l_msghdr *linux_msg, int flags, ssize_t* bytes)
{
	struct cmsghdr *cm;
	struct msghdr msg;
	socklen_t datalen, outlen;
	struct mbuf *control = NULL;
	struct mbuf **controlp = NULL;
	caddr_t outbuf;
	void *data;
	int error;

	error = linux_to_bsd_msghdr(&msg, linux_msg);
	if (error)
		return (error);

	/*
	 * Set msg_flags from flags parameter like sys_recvmsg() for standard behavior.
	 * msg_flags in msghdr passed to kern_recvit() are used as in/out.
	 * msg_flags in msghdr passed to recvmsg() are used for out only and the
	 * flags paramter is used for in.
	 *
	 */
	msg.msg_flags = linux_to_bsd_msg_flags(flags);

	if (msg.msg_name) {
		error = linux_to_bsd_sockaddr((struct bsd_sockaddr *)msg.msg_name,
			msg.msg_namelen);
		if (error)
			goto bad;
	}

	controlp = (msg.msg_control != NULL) ? &control : NULL;
	error = kern_recvit(s, &msg, controlp, bytes);
	if (error)
		goto bad;

	error = bsd_to_linux_msghdr(&msg, linux_msg);
	if (error)
		goto bad;

	if (linux_msg->msg_name) {
		error = bsd_to_linux_sockaddr((struct bsd_sockaddr *)linux_msg->msg_name);
		if (error)
			goto bad;
	}
	if (linux_msg->msg_name && linux_msg->msg_namelen > 2) {
		error = linux_sa_put((bsd_osockaddr*)linux_msg->msg_name);
		if (error)
			goto bad;
	}

	outbuf = (caddr_t)(linux_msg->msg_control);
	outlen = 0;

	if (control && outbuf) {
		struct mbuf *c_mb;
		uint8_t cmsg_buf[L_CMSG_HDRSZ];
		struct l_cmsghdr *linux_cmsg = (struct l_cmsghdr *)cmsg_buf;
		struct in_addr *ipv4_recv_addr = NULL;
		struct bsd_sockaddr_dl *recv_addr_dl = NULL;
		struct timeval *ftmvl;
		l_timeval ltmvl;

		for (c_mb = control; c_mb; c_mb = c_mb->m_hdr.mh_next) {

			msg.msg_control = mtod(c_mb, struct cmsghdr *);
			msg.msg_controllen = c_mb->m_hdr.mh_len;

			cm = CMSG_FIRSTHDR(&msg);

			while (cm != NULL) {
				linux_cmsg->cmsg_type =
					bsd_to_linux_cmsg_type(cm->cmsg_level, cm->cmsg_type);
				linux_cmsg->cmsg_level =
					bsd_to_linux_sockopt_level(cm->cmsg_level);
				if (linux_cmsg->cmsg_type == -1) {
					error = EINVAL;
					goto bad;
				}

				data = CMSG_DATA(cm);
				datalen = (caddr_t)cm + cm->cmsg_len - (caddr_t)data;

				if (cm->cmsg_level == SOL_SOCKET) {
					switch (cm->cmsg_type) {
#if 0
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
#endif
					case SCM_TIMESTAMP:
						if (datalen != sizeof(struct timeval)) {
							error = EMSGSIZE;
							goto bad;
						}
						ftmvl = (struct timeval *)data;
						ltmvl.tv_sec = ftmvl->tv_sec;
						ltmvl.tv_usec = ftmvl->tv_usec;
						data = &ltmvl;
						datalen = sizeof(ltmvl);
						break;
					default:
						error = EINVAL;
						goto bad;
					}
				}
				else if (cm->cmsg_level == IPPROTO_IP) {
					switch (cm->cmsg_type) {
					case IP_RECVIF:
						recv_addr_dl = (struct bsd_sockaddr_dl *) data;
						goto next;
					case IP_RECVDSTADDR:
						ipv4_recv_addr = (struct in_addr *) data;
						goto next;
					default:
						error = EINVAL;
						goto bad;
					}
				}
#ifdef INET6
				else if (cm->cmsg_level == IPPROTO_IPV6) {
					switch (cm->cmsg_type) {
					case IPV6_PKTINFO:
						break;
					default:
						error = EINVAL;
						goto bad;
					}
				}
#endif
				else {
					error = EINVAL;
					goto bad;
				}

				linux_cmsg->cmsg_len = LINUX_CMSG_LEN(datalen);
				error = linux_recvmsg_append_cmsg(outbuf, linux_msg->msg_controllen - outlen,
								  linux_cmsg, (caddr_t)data, datalen);
				if (error) {
					if (outlen) {
						linux_msg->msg_flags |= LINUX_MSG_CTRUNC;
						error = 0;
						goto out;
					}
					goto bad;
				}
				outbuf += LINUX_CMSG_ALIGN(datalen);
				outlen += LINUX_CMSG_LEN(datalen);

next:
				cm = CMSG_NXTHDR(&msg, cm);
			}
		}

		/* FreeBSD doesn't support IP_PKTINFO so build from IP_RECVIF and IP_RECVDSTADDR */
		if (recv_addr_dl && ipv4_recv_addr) {
			struct l_in_pktinfo pktinfo;
			pktinfo.ipi_ifindex = recv_addr_dl->sdl_index;
			pktinfo.ipi_spec_dst.s_addr = 0;
			pktinfo.ipi_addr.s_addr = ipv4_recv_addr->s_addr;
			datalen = sizeof(pktinfo);

			linux_cmsg->cmsg_type = LINUX_IP_PKTINFO;
			linux_cmsg->cmsg_len = LINUX_CMSG_LEN(datalen);
			error = linux_recvmsg_append_cmsg(outbuf, linux_msg->msg_controllen - outlen,
							  linux_cmsg, (caddr_t)&pktinfo, datalen);
			if (error) {
				if (outlen) {
					linux_msg->msg_flags |= LINUX_MSG_CTRUNC;
					error = 0;
					goto out;
				}
				goto bad;
			}
			outbuf += LINUX_CMSG_ALIGN(datalen);
			outlen += LINUX_CMSG_LEN(datalen);
		}
	}

out:
	linux_msg->msg_controllen = outlen;

bad:
	if (control != NULL)
		m_freem(control);

	return (error);
}

int
linux_shutdown(int s, int how)
{
	return (sys_shutdown(s, how));
}

int linux_to_bsd_tcp_sockopt(int name)
{
	// Not using the constants because we never know what will the compiler
	// will insert here. They are interface, so they shouldn't change.
	switch (name) {
	case 1: // TCP_NODELAY
		return 0x001;
	case 3: // TCP_CORK
		return 0x004; // TCP_NOPUSH
	case 4: // TCP_KEEPIDLE
		return 0x100;
	case 5:  // TCP_KEEPINTVL
		return 0x200;
	case 6:  // TCP_KEEPCNT
		return 0x400;
	case 13: // TCP_CONGESTION
		return 0x40;
	}
	// The BSD and Linux constants here are so different, that anything
	// not explicitly supported is not supported. We return -1, which
	// causes our caller to return ENOPROTOOPT
	return -1;
}

int
linux_setsockopt_ip_pktinfo(int s, caddr_t val, int valsize)
{
	int error;

	if ((error = sys_setsockopt(s, IPPROTO_IP, IP_RECVIF, val, valsize)))
		return error;
	return sys_setsockopt(s, IPPROTO_IP, IP_RECVDSTADDR, val, valsize);
}

int
linux_getsockopt_ip_pktinfo(int s, void *val, socklen_t *valsize)
{
	int error;
	int if_enable = 0, addr_enable = 0;
	socklen_t optsize = sizeof(addr_enable);

	if (*valsize < sizeof(addr_enable)) {
		errno = EINVAL;
		return -1;
	}
	if ((error = sys_getsockopt(s, IPPROTO_IP, IP_RECVIF, &if_enable, &optsize)))
		return error;
	if ((error = sys_getsockopt(s, IPPROTO_IP, IP_RECVDSTADDR, &addr_enable, &optsize)))
		return error;
	*(int *) val = (if_enable && addr_enable);
	return 0;
}

int
linux_setsockopt(int s, int level, int name, caddr_t val, int valsize)
{
	int error;

	switch (level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(name);
		break;
	case IPPROTO_IP:
		if (name == LINUX_IP_PKTINFO) {
			return linux_setsockopt_ip_pktinfo(s, val, valsize);
		}
		name = linux_to_bsd_ip_sockopt(name);
		break;
#ifdef INET6
	case IPPROTO_IPV6:
		name = linux_to_bsd_ipv6_sockopt(name);
		break;
#endif
	case IPPROTO_TCP:
		name = linux_to_bsd_tcp_sockopt(name);
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
		linux_to_bsd_sockaddr((struct bsd_sockaddr *)bsd_args.val,
			bsd_args.valsize);
		error = sys_setsockopt(td, &bsd_args);
		bsd_to_linux_sockaddr((struct bsd_sockaddr *)bsd_args.val);
	} else
#endif
		error = sys_setsockopt(s, level, name, val, valsize);

	return (error);
}

int
linux_getsockopt(int s, int level, int name, void *val, socklen_t *valsize)
{
	int error;

	switch (level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(name);
		break;
	case IPPROTO_IP:
		if (name == LINUX_IP_PKTINFO) {
			return linux_getsockopt_ip_pktinfo(s, val, valsize);
		}
		name = linux_to_bsd_ip_sockopt(name);
		break;
#ifdef INET6
	case IPPROTO_IPV6:
		name = linux_to_bsd_ipv6_sockopt(name);
		break;
#endif
	case IPPROTO_TCP:
		name = linux_to_bsd_tcp_sockopt(name);
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
		bsd_to_linux_sockaddr((struct bsd_sockaddr *)bsd_args.val);
	} else
#endif
		error = sys_getsockopt(s, level, name, val, valsize);

	return (error);
}
