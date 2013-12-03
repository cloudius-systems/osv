/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef UIPC_SYSCALLS_H
#define UIPC_SYSCALLS_H

#include <sys/cdefs.h>
#include <osv/file.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/socket.h>

__BEGIN_DECLS

/* Private interface */
int kern_bind(int fd, struct bsd_sockaddr *sa);
int kern_accept(int s, struct bsd_sockaddr *name,
    socklen_t *namelen, struct file **fp, int *out_fd);
int kern_connect(int fd, struct bsd_sockaddr *sa);
int kern_sendit(int s, struct msghdr *mp, int flags,
    struct mbuf *control, ssize_t *bytes);
int kern_recvit(int s, struct msghdr *mp, struct mbuf **controlp, ssize_t* bytes);
int kern_setsockopt(int s, int level, int name, void *val, socklen_t valsize);
int kern_getsockopt(int s, int level, int name, void *val, socklen_t *valsize);
int kern_socketpair(int domain, int type, int protocol, int *rsv);
int kern_getsockname(int fd, struct bsd_sockaddr **sa, socklen_t *alen);

/* FreeBSD Interface */
int sys_socket(int domain, int type, int protocol, int *out_fd);
int sys_bind(int s, struct bsd_sockaddr *sa, int namelen);
int sys_listen(int s, int backlog);
int sys_accept(int s, struct bsd_sockaddr * name, socklen_t * namelen, int *out_fp);
int sys_connect(int s, struct bsd_sockaddr *sa, socklen_t len);
int sys_socketpair(int domain, int type, int protocol, int *rsv);
int sys_sendto(int s, caddr_t buf, size_t  len, int flags, caddr_t to,
    int tolen, ssize_t* bytes);
int sys_sendmsg(int s, struct msghdr* msg, int flags, ssize_t* bytes);
int sys_recvfrom(int s, caddr_t buf, size_t  len, int flags,
    struct bsd_sockaddr * __restrict from, socklen_t * __restrict fromlenaddr,
    ssize_t* bytes);
int sys_recvmsg(int s, struct msghdr *msg, int flags, ssize_t* bytes);
int sys_shutdown(int s, int how);
int sys_setsockopt(int s, int level, int name, caddr_t val, int valsize);
int sys_getsockopt(int s, int level, int name, void * __restrict val,
    socklen_t * __restrict avalsize);
int sys_getsockname(int fdes, struct bsd_sockaddr * __restrict asa, socklen_t * __restrict alen);

/* Linux Interface */
int linux_socket(int domain, int type, int protocol, int *out_fd);
int linux_bind(int s, void *name, int namelen);
int linux_listen(int s, int backlog);
int linux_accept(int s, struct bsd_sockaddr* name, socklen_t* namelen, int *out_fd);
int linux_accept4(int s, struct bsd_sockaddr * name, socklen_t * namelen, int *out_fd, int flags);
int linux_connect(int s, void *name, int namelen);
int linux_sendmsg(int s, struct msghdr* msg, int flags, ssize_t* bytes);
int linux_sendto(int s, void* buf, int len, int flags, void* to, int tolen, ssize_t *bytes);
int linux_send(int s, caddr_t buf, size_t len, int flags, ssize_t* bytes);
int linux_recvmsg(int s, struct msghdr *msg, int flags, ssize_t* bytes);
int linux_recv(int s, caddr_t buf, int len, int flags, ssize_t* bytes);
int linux_recvfrom(int s, void* buf, size_t len, int flags,
	struct bsd_sockaddr * from, socklen_t * fromlen, ssize_t* bytes);
int linux_shutdown(int s, int how);
int linux_setsockopt(int s, int level, int name, caddr_t val, int valsize);
int linux_getsockopt(int s, int level, int name, void *val, socklen_t *valsize);
int linux_socketpair(int domain, int type, int protocol, int* rsv);
int linux_getsockname(int s, struct bsd_sockaddr *addr, socklen_t *addrlen);


__END_DECLS

#endif /* !UIPC_SYSCALLS_H */
