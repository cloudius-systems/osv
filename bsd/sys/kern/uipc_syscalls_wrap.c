#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <bsd/uipc_syscalls.h>
#include <osv/debug.h>
#include "libc/af_local.h"

#include "libc.h"

#define sock_d(...)		tprintf("socket-api", logger_debug, __VA_ARGS__);

int socketpair(int domain, int type, int protocol, int sv[2])
{
	int error;

	sock_d("socketpair(domain=%d, type=%d, protocol=%d)", domain, type,
		protocol);

	if (domain == AF_LOCAL)
		return socketpair_af_local(type, protocol, sv);

	error = linux_socketpair(domain, type, protocol, sv);
	if (error) {
		sock_d("socketpair() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

int getsockname(int sockfd, struct bsd_sockaddr *addr, socklen_t *addrlen)
{
	int error;

	sock_d("getsockname(sockfd=%d, ...)", sockfd);

	error = linux_getsockname(sockfd, addr, addrlen);
	if (error) {
		sock_d("getsockname() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

int accept4(int fd, struct bsd_sockaddr *restrict addr, socklen_t *restrict len, int flg)
{
	int fd2, error;

	sock_d("accept4(fd=%d, ..., flg=%d)", fd, flg);

	error = linux_accept4(fd, addr, len, &fd2, flg);
	if (error) {
		sock_d("accept4() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return fd2;
}

int accept(int fd, struct bsd_sockaddr *restrict addr, socklen_t *restrict len)
{
	int fd2, error;

	sock_d("accept(fd=%d, ...)", fd);

	error = linux_accept(fd, addr, len, &fd2);
	if (error) {
		sock_d("accept() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return fd2;
}

int bind(int fd, const struct bsd_sockaddr *addr, socklen_t len)
{
	int error;

	sock_d("bind(fd=%d, ...)", fd);

	error = linux_bind(fd, (void *)addr, len);
	if (error) {
		sock_d("bind() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

int connect(int fd, const struct bsd_sockaddr *addr, socklen_t len)
{
	int error;

	sock_d("connect(fd=%d, ...)", fd);

	error = linux_connect(fd, (void *)addr, len);
	if (error) {
		sock_d("connect() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

int listen(int fd, int backlog)
{
	int error;

	sock_d("listen(fd=%d, backlog=%d)", fd, backlog);

	error = linux_listen(fd, backlog);
	if (error) {
		sock_d("listen() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

ssize_t recvfrom(int fd, void *restrict buf, size_t len, int flags,
		struct bsd_sockaddr *restrict addr, socklen_t *restrict alen)
{
	int error;
	ssize_t bytes;

	sock_d("recvfrom(fd=%d, buf=<uninit>, len=%d, flags=0x%x, ...)", fd,
		len, flags);

	error = linux_recvfrom(fd, (caddr_t)buf, len, flags, addr, alen, &bytes);
	if (error) {
		sock_d("recvfrom() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return bytes;
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	int error;
	ssize_t bytes;

	sock_d("recv(fd=%d, buf=<uninit>, len=%d, flags=0x%x)", fd, len, flags);

	error = linux_recv(fd, (caddr_t)buf, len, flags, &bytes);
	if (error) {
		sock_d("recv() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return bytes;
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
	ssize_t bytes;
	int error;

	sock_d("recvmsg(fd=%d, msg=..., flags=0x%x)", fd, flags);

	error = linux_recvmsg(fd, msg, flags, &bytes);
	if (error) {
		sock_d("recvmsg() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return bytes;
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
    const struct bsd_sockaddr *addr, socklen_t alen)
{
	int error;
	ssize_t bytes;

	sock_d("sendto(fd=%d, buf=..., len=%d, flags=0x%x, ...", fd, len, flags);

	error = linux_sendto(fd, (caddr_t)buf, len, flags, (caddr_t)addr,
			   alen, &bytes);
	if (error) {
		sock_d("sendto() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return bytes;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	int error;
	ssize_t bytes;

	sock_d("send(fd=%d, buf=..., len=%d, flags=0x%x)", fd, len, flags)

	error = linux_send(fd, (caddr_t)buf, len, flags, &bytes);
	if (error) {
		sock_d("send() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return bytes;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
	ssize_t bytes;
	int error;

	sock_d("sendmsg(fd=%d, msg=..., flags=0x%x)", fd, flags)

	error = linux_sendmsg(fd, (struct msghdr *)msg, flags, &bytes);
	if (error) {
		sock_d("sendmsg() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return bytes;
}

int getsockopt(int fd, int level, int optname, void *restrict optval,
		socklen_t *restrict optlen)
{
	int error;

	sock_d("getsockopt(fd=%d, level=%d, optname=%d)", fd, level, optname);

	error = linux_getsockopt(fd, level, optname, optval, optlen);
	if (error) {
		sock_d("getsockopt() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
	int error;

	sock_d("setsockopt(fd=%d, level=%d, optname=%d, (*(int)optval)=%d, optlen=%d)",
		fd, level, optname, *(int *)optval, optlen);

	error = linux_setsockopt(fd, level, optname, (caddr_t)optval, optlen);
	if (error) {
		sock_d("setsockopt() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

int shutdown(int fd, int how)
{
	int error;

	sock_d("shutdown(fd=%d, how=%d)", fd, how);

	error = linux_shutdown(fd, how);
	if (error) {
		sock_d("shutdown() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return 0;
}

int socket(int domain, int type, int protocol)
{
	int s, error;

	sock_d("socket(domain=%d, type=%d, protocol=%d)", domain, type, protocol);

	error = linux_socket(domain, type, protocol, &s);
	if (error) {
		sock_d("socket() failed, errno=%d", error);
		errno = error;
		return -1;
	}

	return s;
}
