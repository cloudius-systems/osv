/*
 * Copyright (C) 2017 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * This is a test for OSv's IPv6 non-support :-) Although we do not support
 * IPv6, we should return the errors which applications expect - see issue
 * #865 on how us returning the wrong error from socket() confused redis.
 */

#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>


static int tests = 0, fails = 0;
static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}


int main(int argc, char **argv)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    report(sock>=0, "open AF_INET socket succeeds");
    close(sock);
    sock = socket(AF_INET6, SOCK_STREAM, 0);
    report(sock<0, "open AF_INET6 socket fails");
    report(errno==EAFNOSUPPORT, "failure should be EAFNOSUPPORT");
    close(sock);
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails;
}
