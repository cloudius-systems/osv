/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * This is a test for bug #257. We start a TCP client and server (all internal
 * through the loopback interface), the server waits for a connection and
 * writes a long (600,000 byte) response using a single write(), and the client
 * connects to the server and reads the response. We expect the number of
 * bytes that the server thinks it wrote, and the number of bytes actually
 * read by the client, to be identical.
 *
 * The bug we uncovered with this test was when the server's output socket was
 * O_NONBLOCK, it does write some of the output, but erroneously returned -1,
 * not the partial write's size.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <string.h>

#include <thread>


// server() opens a listening TCP server on port 1234, accepts one connection,
// and writes to it with a large optionally non-blocking write()
static constexpr short LISTEN_TCP_PORT = 1234;
static constexpr int nlines = 10000;
static int server(bool nonblock)
{
    int listenfd;
    struct sockaddr_in sa;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("open listening socket");
        return 1;
    };
    int i=1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
    bzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(LISTEN_TCP_PORT);
    if (bind(listenfd, (struct sockaddr *)&sa, sizeof(sa))<0) {
        perror("bind listen_socket");
        abort();
    }
#define CONF_LISTEN_BACKLOG 10
    if (listen(listenfd, CONF_LISTEN_BACKLOG)<0) {
        perror("listen");
        abort();
    }

    char s[52];
    for (int i = 0; i < 51; i++) {
        s[i] = 'x';
    }
    s[51] = '\0';

    int size = nlines * 60;
    char *buf = (char*) malloc(size);
    for (int i = 1; i <= nlines; i++) {
        sprintf(buf + 60*(i-1), "%07d %s\n", i, s);
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int fd = accept(listenfd, (sockaddr*)&client_addr, &client_addr_len);
    if (fd < 0) {
        perror("accept failed");
        close(listenfd);
        return 1;
    }
    if (nonblock) {
        if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
            perror("fcntl");
            abort();
        }
    }
    auto count = write(fd, buf, size);
    close(fd);
    close(listenfd);
    return count;
}

static int client()
{
    int sock;
    struct sockaddr_in sa;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("open socket");
        return 1;
    };
    bzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(LISTEN_TCP_PORT);
    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa))<0) {
        perror("connect socket");
        abort();
    }

    constexpr int bufsize = 4096;
    char buf[bufsize];
    int size = 0;
    int r;
    while ((r = read(sock, buf, bufsize)) > 0) {
        size += r;
    }
    close(sock);
    printf("Client total: read %d\n", size);
    return size;
}

static int test(bool nonblock)
{
    int sent;
    std::thread t([&sent, nonblock] { sent = server(nonblock); });
    sleep(1);
    int received = client();
    t.join();
    printf("Server thinks it sent %d, client received %d\n", sent, received);
    return sent == received;
}

static int tests = 0, fails = 0;
static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}


int main(int argc, char **argv)
{
    report(test(false), "Test with blocking write()");
    report(test(true), "Test with non-blocking write()");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails;
}
