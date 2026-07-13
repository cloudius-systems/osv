/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Live IPv6 internet smoke test: resolve a well-known IPv6-capable host over
// DNS (AAAA), open a TCP connection to it over IPv6, send a minimal HTTP/1.1
// request, and confirm a response comes back.  This exercises the whole IPv6
// path end to end: getaddrinfo/AAAA resolution, IPv6 routing, and an IPv6 TCP
// data transfer against a real server.
//
// Requires external IPv6 networking (QEMU SLIRP with ipv6=on, or a tap device
// with IPv6).  Run manually via run.py with an ipv6=on user netdev and a
// nameserver; not part of the automated suite (needs the network).

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static const char *HOST = "ipv6.google.com";   // AAAA-only hostname
static const char *PORT = "80";

int main(int argc, char **argv)
{
    const char *host = (argc > 1) ? argv[1] : HOST;
    fprintf(stderr, "Running ipv6-internet test against %s\n", host);

    // 1) DNS: resolve an AAAA record via getaddrinfo restricted to AF_INET6.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;       // force IPv6 / AAAA lookup
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, PORT, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "FAIL: getaddrinfo(%s) AAAA: %s\n", host, gai_strerror(gai));
        return 1;
    }

    // Print the resolved IPv6 address(es).
    int connected = -1;
    // On IPv6-only links neighbor discovery / DAD can take a few seconds to
    // settle before the gateway is reachable, so retry the connect for a while
    // rather than giving up on the first N.D.-in-progress failure.
    for (int attempt = 0; attempt < 30 && connected < 0; attempt++) {
      for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        char abuf[INET6_ADDRSTRLEN] = {0};
        auto *sin6 = (struct sockaddr_in6 *)ai->ai_addr;
        inet_ntop(AF_INET6, &sin6->sin6_addr, abuf, sizeof(abuf));
        if (attempt == 0)
            fprintf(stderr, "  resolved %s -> [%s]:%s\n", host, abuf, PORT);

        int s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) {
            continue;
        }
        if (connect(s, ai->ai_addr, ai->ai_addrlen) == 0) {
            connected = s;
            break;
        }
        if (attempt == 29)
            fprintf(stderr, "  connect to [%s] failed: %s\n", abuf, strerror(errno));
        close(s);
      }
      if (connected < 0)
          sleep(1);   // let ND / DAD settle, then retry
    }
    freeaddrinfo(res);

    if (connected < 0) {
        fprintf(stderr, "FAIL: could not establish an IPv6 connection to %s\n", host);
        return 1;
    }

    // 2) Minimal HTTP request over the IPv6 connection.
    char req[256];
    int rl = snprintf(req, sizeof(req),
                      "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                      host);
    if (write(connected, req, rl) != rl) {
        fprintf(stderr, "FAIL: write to IPv6 socket: %s\n", strerror(errno));
        close(connected);
        return 1;
    }

    // 3) Read the response and confirm it looks like HTTP.
    char buf[512];
    ssize_t n = read(connected, buf, sizeof(buf) - 1);
    close(connected);
    if (n <= 0) {
        fprintf(stderr, "FAIL: no response over IPv6: %s\n", strerror(errno));
        return 1;
    }
    buf[n] = '\0';
    fprintf(stderr, "  got %zd bytes, first line: %.40s\n", n, buf);
    if (strncmp(buf, "HTTP/", 5) != 0) {
        fprintf(stderr, "FAIL: response is not HTTP\n");
        return 1;
    }

    fprintf(stderr, "ipv6-internet test PASSED (DNS AAAA + IPv6 TCP + HTTP)\n");
    return 0;
}
