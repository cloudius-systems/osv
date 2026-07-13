/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Self-contained smoke test for the IPv6 port: verifies AF_INET6 sockets work,
// that a loopback IPv6 TCP connection carries data both ways, and that an IPv4
// connection still works concurrently.  Deliberately plain C (no boost), so it
// runs without the boost unit-test framework.

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

// Run a one-shot echo server on `family` bound to loopback, hand the chosen
// port back through *port, and return the listening fd.
static int start_server(int family, int *port)
{
    int s = socket(family, SOCK_STREAM, 0);
    assert(s >= 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    socklen_t alen;
    if (family == AF_INET6) {
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_loopback;
        a.sin6_port = 0;
        assert(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0);
        alen = sizeof(a);
        assert(getsockname(s, (struct sockaddr *)&a, &alen) == 0);
        *port = ntohs(a.sin6_port);
    } else {
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        assert(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0);
        alen = sizeof(a);
        assert(getsockname(s, (struct sockaddr *)&a, &alen) == 0);
        *port = ntohs(a.sin_port);
    }
    assert(listen(s, 4) == 0);
    return s;
}

// Accept one connection, echo one message back, close.  Runs in a thread.
static void *echo_once(void *arg)
{
    int listen_fd = *(int *)arg;
    int c = accept(listen_fd, NULL, NULL);
    assert(c >= 0);
    char buf[128];
    ssize_t n = read(c, buf, sizeof(buf));
    assert(n > 0);
    assert(write(c, buf, n) == n);   // echo
    close(c);
    return NULL;
}

// Connect to loopback:port on `family`, send msg, verify the echo matches.
static void client_roundtrip(int family, int port, const char *msg)
{
    int c = socket(family, SOCK_STREAM, 0);
    assert(c >= 0);
    if (family == AF_INET6) {
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_loopback;
        a.sin6_port = htons(port);
        assert(connect(c, (struct sockaddr *)&a, sizeof(a)) == 0);
    } else {
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(port);
        assert(connect(c, (struct sockaddr *)&a, sizeof(a)) == 0);
    }
    size_t len = strlen(msg);
    assert(write(c, msg, len) == (ssize_t)len);
    char buf[128];
    ssize_t n = read(c, buf, sizeof(buf));
    assert(n == (ssize_t)len);
    assert(memcmp(buf, msg, len) == 0);
    close(c);
}

static void roundtrip_on(int family, const char *label, const char *msg)
{
    int port = 0;
    int lfd = start_server(family, &port);
    pthread_t th;
    assert(pthread_create(&th, NULL, echo_once, &lfd) == 0);
    client_roundtrip(family, port, msg);
    pthread_join(th, NULL);
    close(lfd);
    fprintf(stderr, "  %s loopback echo OK (port %d)\n", label, port);
}

int main()
{
    fprintf(stderr, "Running tcp-v6-smoke tests\n");

    // AF_INET6 sockets must be creatable (the whole point of the port).
    int s6 = socket(AF_INET6, SOCK_STREAM, 0);
    assert(s6 >= 0);
    close(s6);

    // IPv6 loopback TCP round-trip.
    roundtrip_on(AF_INET6, "IPv6", "hello over ipv6 loopback");

    // IPv4 must still work (no regression from adding IPv6).
    roundtrip_on(AF_INET, "IPv4", "hello over ipv4 loopback");

    // And concurrently: an IPv6 and an IPv4 connection at the same time.
    {
        int p4 = 0, p6 = 0;
        int l4 = start_server(AF_INET, &p4);
        int l6 = start_server(AF_INET6, &p6);
        pthread_t t4, t6;
        pthread_create(&t4, NULL, echo_once, &l4);
        pthread_create(&t6, NULL, echo_once, &l6);
        client_roundtrip(AF_INET6, p6, "concurrent v6");
        client_roundtrip(AF_INET, p4, "concurrent v4");
        pthread_join(t4, NULL);
        pthread_join(t6, NULL);
        close(l4);
        close(l6);
        fprintf(stderr, "  concurrent IPv4 + IPv6 OK\n");
    }

    // Exercise the wider IPv6 API surface a real app relies on, proving the
    // symbols resolve and the option handlers behave.
    {
        int s = socket(AF_INET6, SOCK_STREAM, 0);
        assert(s >= 0);

        // IPV6_V6ONLY: dual-stack control. Toggle and read back.
        int v = 1;
        assert(setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v, sizeof(v)) == 0);
        int got = 0;
        socklen_t gl = sizeof(got);
        assert(getsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &got, &gl) == 0);
        assert(got == 1);

        // IPV6_UNICAST_HOPS: set/get hop limit.
        int hops = 42;
        assert(setsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hops, sizeof(hops)) == 0);
        got = 0; gl = sizeof(got);
        assert(getsockopt(s, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &got, &gl) == 0);
        assert(got == 42);

        // IPV6_RECVPKTINFO: ancillary-data request must be accepted.
        v = 1;
        assert(setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO, &v, sizeof(v)) == 0);
        close(s);
        fprintf(stderr, "  IPV6_V6ONLY / UNICAST_HOPS / RECVPKTINFO OK\n");
    }

    // Multicast group membership (IPV6_JOIN_GROUP) on the loopback interface.
    {
        int s = socket(AF_INET6, SOCK_DGRAM, 0);
        assert(s >= 0);
        struct ipv6_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        // ff02::1 = all-nodes link-local multicast.
        assert(inet_pton(AF_INET6, "ff02::1", &mreq.ipv6mr_multiaddr) == 1);
        mreq.ipv6mr_interface = if_nametoindex("lo0");
        int r = setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq));
        // Join may fail if the loopback has no v6 multicast route, but the
        // option must be recognized (not EINVAL/ENOPROTOOPT).
        assert(r == 0 || (errno != ENOPROTOOPT && errno != EINVAL));
        close(s);
        fprintf(stderr, "  IPV6_JOIN_GROUP recognized (rc=%d)\n", r);
    }

    // getaddrinfo of an IPv6 literal (no network needed) exercises the
    // AAAA/AF_INET6 resolution path and sockaddr_in6 construction.
    {
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET6;
        hints.ai_flags = AI_NUMERICHOST;
        hints.ai_socktype = SOCK_STREAM;
        assert(getaddrinfo("::1", "80", &hints, &res) == 0);
        assert(res && res->ai_family == AF_INET6);
        char abuf[INET6_ADDRSTRLEN] = {0};
        auto *sin6 = (struct sockaddr_in6 *)res->ai_addr;
        assert(inet_ntop(AF_INET6, &sin6->sin6_addr, abuf, sizeof(abuf)));
        assert(strcmp(abuf, "::1") == 0);
        freeaddrinfo(res);
        fprintf(stderr, "  getaddrinfo(::1) AAAA path OK\n");
    }

    fprintf(stderr, "tcp-v6-smoke tests PASSED\n");
    return 0;
}
