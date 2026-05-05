/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV__SOCKET_H_
#define OSV__SOCKET_H_

#include <bits/socket.h>
#include <endian.h>

/* struct msghdr and cmsghdr: moved from arch-specific bits/socket.h in musl 1.2.x */
#ifndef __DEFINED_struct_msghdr
#define __DEFINED_struct_msghdr
struct msghdr {
	void *msg_name;
	socklen_t msg_namelen;
	struct iovec *msg_iov;
#if __BYTE_ORDER == __BIG_ENDIAN
	int __pad1, msg_iovlen;
#else
	int msg_iovlen, __pad1;
#endif
	void *msg_control;
#if __BYTE_ORDER == __BIG_ENDIAN
	int __pad2;
	socklen_t msg_controllen;
#else
	socklen_t msg_controllen;
	int __pad2;
#endif
	int msg_flags;
};
struct cmsghdr {
#if __BYTE_ORDER == __BIG_ENDIAN
	int __pad1;
	socklen_t cmsg_len;
#else
	socklen_t cmsg_len;
	int __pad1;
#endif
	int cmsg_level;
	int cmsg_type;
};
#endif /* __DEFINED_struct_msghdr */

struct linger
{
        int l_onoff;
        int l_linger;
};

#ifndef SOL_SOCKET
#define SOL_SOCKET      1
#endif

#endif /* OSV__SOCKET_H_ */
