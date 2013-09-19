/* IPV4 DNS resolver regression tests
 *
 * Copyright (C) 2013 Nodalink, SARL.
 *
 * author: Benoît Canet <benoit.canet@irqsave.net>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static bool test_getaddrinfo(const char *hostname, const char *ip)
{
    struct addrinfo hints, *result;
    char r_ip[INET_ADDRSTRLEN];
    int ret = 0;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    ret = getaddrinfo(hostname, "80", &hints, &result);
    if (ret) {
        return false;
    }

    if (!result) {
        return false;
    }

    /* take the first result */
    inet_ntop(AF_INET,
              &((sockaddr_in *) result->ai_addr)->sin_addr,
              r_ip,
              INET_ADDRSTRLEN);

    freeaddrinfo(result);
    return !strcmp(r_ip, ip);
}

static bool test_getnameinfo(const char *ip, const char *hostname)
{
    char r_hostname[NI_MAXHOST];
    struct sockaddr_in sa_in;
    int ret = 0;

    memset(&sa_in, 0, sizeof(sa_in));
    sa_in.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &sa_in.sin_addr);

    ret = getnameinfo((const sockaddr*) &sa_in,
                      sizeof(sa_in),
                      r_hostname,
                      sizeof(r_hostname),
                      NULL,
                      0,
                      /* retrieve only the hostname part from local hosts */
                      NI_NAMEREQD|NI_NOFQDN);

    if (ret) {
        return false;
    }

    return !strcmp(r_hostname, hostname);
}

int main(int argc, char *argv[])
{
    report(test_getaddrinfo("localhost", "127.0.0.1"),
                            "getaddrinfo(\"localhost\") == \"127.0.0.1\"");
    report(test_getaddrinfo("c.root-servers.net", "192.33.4.12"),
                            "getaddrinfo(\"c.root-servers.net\") == "
                            "\"192.33.4.12\"");
    report(test_getnameinfo("127.0.0.1",
                            "localhost"),
                            "getnameinfo(\"127.0.0.1\") == \"localhost\"");
    report(test_getnameinfo("192.33.4.12",
                            "c.root-servers.net"),
                            "getnameinfo(\"192.33.4.12\") == "
                            "\"c.root-servers.net\"");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
