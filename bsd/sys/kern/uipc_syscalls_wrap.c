
#include <stdint.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/uipc_syscalls.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "libc.h"

int accept4(int fd, struct sockaddr *restrict addr, socklen_t *restrict len, int flg)
{
	int fd2, error;

	/* In Release, this flag will be ignored */
	assert(((flg & SOCK_CLOEXEC) == 0));

	error = sys_accept(fd, addr, *len, &fd2);
	if (error) {
		errno = error;
		return -1;
	}

	if (flg & SOCK_NONBLOCK) {
		fcntl(fd, F_SETFL, O_NONBLOCK);
	}

	return fd2;
}

int accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
	int fd2, error;

	error = sys_accept(fd, addr, *len, &fd2);
	if (error) {
		errno = error;
		return -1;
	}

	return fd2;
}

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	int error;

	error = sys_bind(fd, (struct sockaddr *)addr, len);
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	int error;

	/* FIXME: what to do with len? */
	error = sys_connect(fd, (struct sockaddr *)addr);
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int listen(int fd, int backlog)
{
	int error;

	error = sys_listen(fd, backlog);
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

ssize_t recvfrom(int fd, void *restrict buf, size_t len, int flags,
		struct sockaddr *restrict addr, socklen_t *restrict alen)
{
	int error;
	ssize_t bytes;

	error = sys_recvfrom(fd, (caddr_t)buf, len, flags, addr, alen, &bytes);
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	return recvfrom(fd, buf, len, flags, 0, 0);
}


ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
	ssize_t bytes;
	int error;

	error = sys_recvmsg(fd, msg, flags, &bytes);
	if (error) {
		errno = error;
		return -1;
	}

	return bytes;
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
    const struct sockaddr *addr, socklen_t alen)
{
	int error;
	ssize_t bytes;

	error = sys_sendto(fd, (caddr_t)buf, len, flags, (caddr_t)addr,
			   alen, &bytes);
	if (error) {
		errno = error;
		return -1;
	}

	return bytes;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return sendto(fd, buf, len, flags, 0, 0);
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
	ssize_t bytes;
	int error;

	error = sys_sendmsg(fd, (struct msghdr *)msg, flags, &bytes);
	if (error) {
		errno = error;
		return -1;
	}

	return bytes;
}

int getsockopt(int fd, int level, int optname, void *restrict optval,
		socklen_t *restrict optlen)
{
	int error;
	error = sys_getsockopt(fd, level, optname, optval, optlen);
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
	int error;

	error = sys_setsockopt(fd, level, optname, (caddr_t)optval, optlen);
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int shutdown(int fd, int how)
{
	int error;

	error = sys_shutdown(fd, how);
	if (error) {
		errno = error;
		return -1;
	}

	return 0;
}

int socket(int domain, int type, int protocol)
{
	int s, error;

	/* This goes in release... */
	assert(((type & SOCK_CLOEXEC) == 0));

	error = sys_socket(domain, (type & ~SOCK_NONBLOCK), protocol, &s);
	if (error) {
		errno = error;
		return -1;
	}

	if (type & SOCK_NONBLOCK)
		fcntl(s, F_SETFD, O_NONBLOCK);

	return s;
}
