#ifndef UIPC_SYSCALLS_H
#define UIPC_SYSCALLS_H

#include <osv/file.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/socket.h>

/* Private interface */
int kern_bind(int fd, struct sockaddr *sa);
int kern_accept(int s, struct sockaddr **name,
    socklen_t *namelen, struct file **fp, int *out_fd);
int kern_connect(int fd, struct sockaddr *sa);
int kern_sendit(int s, struct msghdr *mp, int flags,
    struct mbuf *control, ssize_t *bytes);
int kern_recvit(int s, struct msghdr *mp, struct mbuf **controlp, ssize_t* bytes);
int kern_setsockopt(int s, int level, int name, void *val, socklen_t valsize);
int kern_getsockopt(int s, int level, int name, void *val, socklen_t *valsize);

/* FreeBSD Interface */
int sys_socket(int domain, int type, int protocol, int *out_fd);
int sys_bind(int s, struct sockaddr *sa, int namelen);
int sys_listen(int s, int backlog);
int sys_accept(int s, struct sockaddr * name, socklen_t * namelen, int *out_fp);
int sys_connect(int s, struct sockaddr *sa, socklen_t len);
int sys_socketpair(int domain, int type, int protocol, int *rsv);
int sys_sendto(int s, caddr_t buf, size_t  len, int flags, caddr_t to,
    int tolen, ssize_t* bytes);
int sys_sendmsg(int s, struct msghdr* msg, int flags, ssize_t* bytes);
int sys_recvfrom(int s, caddr_t buf, size_t  len, int flags,
    struct sockaddr * __restrict from, socklen_t * __restrict fromlenaddr,
    ssize_t* bytes);
int sys_recvmsg(int s, struct msghdr *msg, int flags, ssize_t* bytes);
int sys_shutdown(int s, int how);
int sys_setsockopt(int s, int level, int name, caddr_t val, int valsize);
int sys_getsockopt(int s, int level, int name, void * __restrict val,
    socklen_t * __restrict avalsize);

#endif /* !UIPC_SYSCALLS_H */
