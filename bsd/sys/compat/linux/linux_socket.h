/*-
 * Copyright (c) 2000 Assar Westerlund
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
 *
 * $FreeBSD$
 */

#ifndef _LINUX_SOCKET_H_
#define _LINUX_SOCKET_H_

/* msg flags in recvfrom/recvmsg */

#define LINUX_MSG_OOB		0x01
#define LINUX_MSG_PEEK		0x02
#define LINUX_MSG_DONTROUTE	0x04
#define LINUX_MSG_CTRUNC	0x08
#define LINUX_MSG_PROXY		0x10
#define LINUX_MSG_TRUNC		0x20
#define LINUX_MSG_DONTWAIT	0x40
#define LINUX_MSG_EOR		0x80
#define LINUX_MSG_WAITALL	0x100
#define LINUX_MSG_FIN		0x200
#define LINUX_MSG_SYN		0x400
#define LINUX_MSG_CONFIRM	0x800
#define LINUX_MSG_RST		0x1000
#define LINUX_MSG_ERRQUEUE	0x2000
#define LINUX_MSG_NOSIGNAL	0x4000
#define LINUX_MSG_CMSG_CLOEXEC	0x40000000

/* Socket-level control message types */

#define LINUX_SCM_RIGHTS	0x01
#define LINUX_SCM_CREDENTIALS   0x02
#define LINUX_SCM_TIMESTAMP	0x1D

struct l_msghdr {
	l_uintptr_t     msg_name;
	l_int           msg_namelen;
	l_uintptr_t     msg_iov;
	l_size_t        msg_iovlen;
	l_uintptr_t     msg_control;
	l_size_t        msg_controllen;
	l_uint          msg_flags;
};

struct l_mmsghdr {
	struct l_msghdr msg_hdr;
	l_uint          msg_len;

};

struct l_cmsghdr {
	l_size_t        cmsg_len;
	l_int           cmsg_level;
	l_int           cmsg_type;
};

/* Ancilliary data object information macros */

#define LINUX_CMSG_ALIGN(len)	roundup2(len, sizeof(l_ulong))
#define LINUX_CMSG_DATA(cmsg)	((void *)((char *)(cmsg) + \
				    LINUX_CMSG_ALIGN(sizeof(struct l_cmsghdr))))
#define LINUX_CMSG_SPACE(len)	(LINUX_CMSG_ALIGN(sizeof(struct l_cmsghdr)) + \
				    LINUX_CMSG_ALIGN(len))
#define LINUX_CMSG_LEN(len)	(LINUX_CMSG_ALIGN(sizeof(struct l_cmsghdr)) + \
				    (len))
#define LINUX_CMSG_FIRSTHDR(msg) \
				((msg)->msg_controllen >= \
				    sizeof(struct l_cmsghdr) ? \
				    (struct l_cmsghdr *) \
				        ((msg)->msg_control) : \
				    (struct l_cmsghdr *)(NULL))
#define LINUX_CMSG_NXTHDR(msg, cmsg) \
				((((char *)(cmsg) + \
				    LINUX_CMSG_ALIGN((cmsg)->cmsg_len) + \
				    sizeof(*(cmsg))) > \
				    (((char *)((msg)->msg_control)) + \
				    (msg)->msg_controllen)) ? \
				    (struct l_cmsghdr *) NULL : \
				    (struct l_cmsghdr *)((char *)(cmsg) + \
				    LINUX_CMSG_ALIGN((cmsg)->cmsg_len)))

#define CMSG_HDRSZ		CMSG_LEN(0)
#define L_CMSG_HDRSZ		LINUX_CMSG_LEN(0)

/* Supported address families */

#define	LINUX_AF_UNSPEC		0
#define	LINUX_AF_UNIX		1
#define	LINUX_AF_INET		2
#define	LINUX_AF_AX25		3
#define	LINUX_AF_IPX		4
#define	LINUX_AF_APPLETALK	5
#define	LINUX_AF_INET6		10
#define LINUX_AF_NETLINK	16
#define LINUX_AF_PACKET		17

/* Supported socket types */

#define	LINUX_SOCK_STREAM	1
#define	LINUX_SOCK_DGRAM	2
#define	LINUX_SOCK_RAW		3
#define	LINUX_SOCK_RDM		4
#define	LINUX_SOCK_SEQPACKET	5

#define	LINUX_SOCK_MAX		LINUX_SOCK_SEQPACKET

#define	LINUX_SOCK_TYPE_MASK	0xf

/* Flags for socket, socketpair, accept4 */

#define	LINUX_SOCK_CLOEXEC	LINUX_O_CLOEXEC
#define	LINUX_SOCK_NONBLOCK	LINUX_O_NONBLOCK

struct l_ucred {
	uint32_t	pid;
	uint32_t	uid;
	uint32_t	gid;
};

struct l_in_addr {
	uint32_t	s_addr;
};

struct l_in_pktinfo {
	l_uint			ipi_ifindex;
	struct l_in_addr	ipi_spec_dst;
	struct l_in_addr	ipi_addr;
};

/* Socket options */
#define	LINUX_IP_TOS		1
#define	LINUX_IP_TTL		2
#define	LINUX_IP_HDRINCL	3
#define	LINUX_IP_OPTIONS	4
#define LINUX_IP_ROUTER_ALERT	5
#define LINUX_IP_RECVOPTS	6
#define LINUX_IP_RETOPTS	7
#define LINUX_IP_PKTINFO	8

#define	LINUX_IP_MULTICAST_IF		32
#define	LINUX_IP_MULTICAST_TTL		33
#define	LINUX_IP_MULTICAST_LOOP		34
#define	LINUX_IP_ADD_MEMBERSHIP		35
#define	LINUX_IP_DROP_MEMBERSHIP	36


#define LINUX_IPV6_ADDRFORM		1
#define LINUX_IPV6_2292PKTINFO		2
#define LINUX_IPV6_2292HOPOPTS		3
#define LINUX_IPV6_2292DSTOPTS		4
#define LINUX_IPV6_2292RTHDR		5
#define LINUX_IPV6_2292PKTOPTIONS	6
#define LINUX_IPV6_CHECKSUM		7
#define LINUX_IPV6_2292HOPLIMIT		8

#define LINUX_SCM_SRCRT			LINUX_IPV6_RXSRCRT

#define LINUX_IPV6_NEXTHOP		9
#define LINUX_IPV6_AUTHHDR		10
#define LINUX_IPV6_UNICAST_HOPS		16
#define LINUX_IPV6_MULTICAST_IF		17
#define LINUX_IPV6_MULTICAST_HOPS	18
#define LINUX_IPV6_MULTICAST_LOOP	19
#define LINUX_IPV6_JOIN_GROUP		20
#define LINUX_IPV6_LEAVE_GROUP		21
#define LINUX_IPV6_ROUTER_ALERT		22
#define LINUX_IPV6_MTU_DISCOVER		23
#define LINUX_IPV6_MTU			24
#define LINUX_IPV6_RECVERR		25
#define LINUX_IPV6_V6ONLY		26
#define LINUX_IPV6_JOIN_ANYCAST		27
#define LINUX_IPV6_LEAVE_ANYCAST	28
#define LINUX_IPV6_IPSEC_POLICY		34
#define LINUX_IPV6_XFRM_POLICY		35

/* Advanced API (RFC3542) (1).  */
#define LINUX_IPV6_RECVPKTINFO		49
#define LINUX_IPV6_PKTINFO		50
#define LINUX_IPV6_RECVHOPLIMIT		51
#define LINUX_IPV6_HOPLIMIT		52
#define LINUX_IPV6_RECVHOPOPTS		53
#define LINUX_IPV6_HOPOPTS		54
#define LINUX_IPV6_RTHDRDSTOPTS		55
#define LINUX_IPV6_RECVRTHDR		56
#define LINUX_IPV6_RTHDR		57
#define LINUX_IPV6_RECVDSTOPTS		58
#define LINUX_IPV6_DSTOPTS		59
#define LINUX_IPV6_RECVPATHMTU		60
#define LINUX_IPV6_PATHMTU		61
#define LINUX_IPV6_DONTFRAG		62

/* Advanced API (RFC3542) (2).  */
#define LINUX_IPV6_RECVTCLASS		66
#define LINUX_IPV6_TCLASS		67

/* Obsolete synonyms for the above.  */
#define LINUX_IPV6_ADD_MEMBERSHIP	LINUX_IPV6_JOIN_GROUP
#define LINUX_IPV6_DROP_MEMBERSHIP	LINUX_IPV6_LEAVE_GROUP
#define LINUX_IPV6_RXHOPOPTS		LINUX_IPV6_HOPOPTS
#define LINUX_IPV6_RXDSTOPTS		LINUX_IPV6_DSTOPTS


#endif /* _LINUX_SOCKET_H_ */
