/*-
 * Copyright (c) 2004 Tim J. Robbins
 * Copyright (c) 2001 Doug Rabson
 * Copyright (c) 1994-1996 Soren Schmidt
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

#ifndef _AMD64_LINUX_H_
#define	_AMD64_LINUX_H_

/*
 * Provide a separate set of types for the Linux types.
 */
typedef int		l_int;
typedef int32_t		l_long;
typedef int64_t		l_longlong;
typedef short		l_short;
typedef unsigned int	l_uint;
typedef uint32_t	l_ulong;
typedef uint64_t	l_ulonglong;
typedef unsigned short	l_ushort;

typedef l_ulong		l_uintptr_t;
typedef l_long		l_clock_t;
typedef l_int		l_daddr_t;
typedef l_ushort	l_dev_t;
typedef l_uint		l_gid_t;
typedef l_ushort	l_gid16_t;
typedef l_ulong		l_ino_t;
typedef l_int		l_key_t;
typedef l_longlong	l_loff_t;
typedef l_ushort	l_mode_t;
typedef l_long		l_off_t;
typedef l_int		l_pid_t;
typedef l_uint		l_size_t;
typedef l_long		l_suseconds_t;
typedef l_long		l_time_t;
typedef l_uint		l_uid_t;
typedef l_ushort	l_uid16_t;
typedef l_int		l_timer_t;
typedef l_int		l_mqd_t;

typedef struct {
	l_int		val[2];
} __packed l_fsid_t;

typedef struct {
	l_time_t	tv_sec;
	l_suseconds_t	tv_usec;
} l_timeval;

#define	l_fd_set	fd_set
/*
 * Socket defines
 */
#define	LINUX_SOCKET 		1
#define	LINUX_BIND		2
#define	LINUX_CONNECT 		3
#define	LINUX_LISTEN 		4
#define	LINUX_ACCEPT 		5
#define	LINUX_GETSOCKNAME	6
#define	LINUX_GETPEERNAME	7
#define	LINUX_SOCKETPAIR	8
#define	LINUX_SEND		9
#define	LINUX_RECV		10
#define	LINUX_SENDTO 		11
#define	LINUX_RECVFROM 		12
#define	LINUX_SHUTDOWN 		13
#define	LINUX_SETSOCKOPT	14
#define	LINUX_GETSOCKOPT	15
#define	LINUX_SENDMSG		16
#define	LINUX_RECVMSG		17
#define	LINUX_ACCEPT4		18

#define	LINUX_SOL_SOCKET	1
#define	LINUX_SOL_IP		0
#define	LINUX_SOL_IPX		256
#define	LINUX_SOL_AX25		257
#define	LINUX_SOL_TCP		6
#define	LINUX_SOL_UDP		17

#define	LINUX_SO_DEBUG		1
#define	LINUX_SO_REUSEADDR	2
#define	LINUX_SO_TYPE		3
#define	LINUX_SO_ERROR		4
#define	LINUX_SO_DONTROUTE	5
#define	LINUX_SO_BROADCAST	6
#define	LINUX_SO_SNDBUF		7
#define	LINUX_SO_RCVBUF		8
#define	LINUX_SO_KEEPALIVE	9
#define	LINUX_SO_OOBINLINE	10
#define	LINUX_SO_NO_CHECK	11
#define	LINUX_SO_PRIORITY	12
#define	LINUX_SO_LINGER		13
#define	LINUX_SO_PEERCRED	17
#define	LINUX_SO_RCVLOWAT	18
#define	LINUX_SO_SNDLOWAT	19
#define	LINUX_SO_RCVTIMEO	20
#define	LINUX_SO_SNDTIMEO	21
#define	LINUX_SO_TIMESTAMP	29
#define	LINUX_SO_ACCEPTCONN	30

#define	LINUX_IP_TOS		1
#define	LINUX_IP_TTL		2
#define	LINUX_IP_HDRINCL	3
#define	LINUX_IP_OPTIONS	4

#define	LINUX_IP_MULTICAST_IF		32
#define	LINUX_IP_MULTICAST_TTL		33
#define	LINUX_IP_MULTICAST_LOOP		34
#define	LINUX_IP_ADD_MEMBERSHIP		35
#define	LINUX_IP_DROP_MEMBERSHIP	36

struct l_sockaddr {
	l_ushort	sa_family;
	char		sa_data[14];
} __packed;

struct l_cmsghdr {
	l_size_t	cmsg_len;
	l_int		cmsg_level;
	l_int		cmsg_type;
};

struct l_ifmap {
	l_ulong		mem_start;
	l_ulong		mem_end;
	l_ushort	base_addr;
	u_char		irq;
	u_char		dma;
	u_char		port;
} __packed;

#define	LINUX_IFHWADDRLEN	6
#define	LINUX_IFNAMSIZ		16

struct l_ifreq {
	union {
		char	ifrn_name[LINUX_IFNAMSIZ];
	} ifr_ifrn;

	union {
		struct l_sockaddr	ifru_addr;
		struct l_sockaddr	ifru_dstaddr;
		struct l_sockaddr	ifru_broadaddr;
		struct l_sockaddr	ifru_netmask;
		struct l_sockaddr	ifru_hwaddr;
		l_short		ifru_flags[1];
		l_int		ifru_metric;
		l_int		ifru_mtu;
		struct l_ifmap	ifru_map;
		char		ifru_slave[LINUX_IFNAMSIZ];
		l_uintptr_t	ifru_data;
	} ifr_ifru;
} __packed;

#define	ifr_name	ifr_ifrn.ifrn_name	/* Interface name */
#define	ifr_hwaddr	ifr_ifru.ifru_hwaddr	/* MAC address */

struct l_ifconf {
	int	ifc_len;
	union {
		l_uintptr_t	ifcu_buf;
		l_uintptr_t	ifcu_req;
	} ifc_ifcu;
} __packed;

#define	ifc_buf		ifc_ifcu.ifcu_buf
#define	ifc_req		ifc_ifcu.ifcu_req

/*
 * poll()
 */
#define	LINUX_POLLIN		0x0001
#define	LINUX_POLLPRI		0x0002
#define	LINUX_POLLOUT		0x0004
#define	LINUX_POLLERR		0x0008
#define	LINUX_POLLHUP		0x0010
#define	LINUX_POLLNVAL		0x0020
#define	LINUX_POLLRDNORM	0x0040
#define	LINUX_POLLRDBAND	0x0080
#define	LINUX_POLLWRNORM	0x0100
#define	LINUX_POLLWRBAND	0x0200
#define	LINUX_POLLMSG		0x0400

struct l_pollfd {
	l_int		fd;
	l_short		events;
	l_short		revents;
} __packed;

struct iovec;

struct l_iovec32 {
	uint32_t	iov_base;
	l_size_t	iov_len;
};

int linux32_copyiniov(struct l_iovec32 *iovp32, l_ulong iovcnt,
			    struct iovec **iovp, int error);

/* robust futexes */
struct linux_robust_list {
	l_uintptr_t			next;
};

struct linux_robust_list_head {
	struct linux_robust_list	list;
	l_long				futex_offset;
	l_uintptr_t			pending_list;
};

int linux_set_upcall_kse(struct thread *td, register_t stack);
int linux_set_cloned_tls(struct thread *td, void *desc);



/*
 * open/fcntl flags
 */
#define	LINUX_O_RDONLY		00000000
#define	LINUX_O_WRONLY		00000001
#define	LINUX_O_RDWR		00000002
#define	LINUX_O_ACCMODE		00000003
#define	LINUX_O_CREAT		00000100
#define	LINUX_O_EXCL		00000200
#define	LINUX_O_NOCTTY		00000400
#define	LINUX_O_TRUNC		00001000
#define	LINUX_O_APPEND		00002000
#define	LINUX_O_NONBLOCK	00004000
#define	LINUX_O_NDELAY		LINUX_O_NONBLOCK
#define	LINUX_O_SYNC		00010000
#define	LINUX_FASYNC		00020000
#define	LINUX_O_DIRECT		00040000	/* Direct disk access hint */
#define	LINUX_O_LARGEFILE	00100000
#define	LINUX_O_DIRECTORY	00200000	/* Must be a directory */
#define	LINUX_O_NOFOLLOW	00400000	/* Do not follow links */
#define	LINUX_O_NOATIME		01000000
#define	LINUX_O_CLOEXEC		02000000

#define	LINUX_F_DUPFD		0
#define	LINUX_F_GETFD		1
#define	LINUX_F_SETFD		2
#define	LINUX_F_GETFL		3
#define	LINUX_F_SETFL		4
#define	LINUX_F_GETLK		5
#define	LINUX_F_SETLK		6
#define	LINUX_F_SETLKW		7
#define	LINUX_F_SETOWN		8
#define	LINUX_F_GETOWN		9

#define	LINUX_F_GETLK64		12
#define	LINUX_F_SETLK64		13
#define	LINUX_F_SETLKW64	14

#define	LINUX_F_RDLCK		0
#define	LINUX_F_WRLCK		1
#define	LINUX_F_UNLCK		2


#endif /* !_AMD64_LINUX_H_ */
