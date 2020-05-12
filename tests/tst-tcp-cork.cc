/*
 * Copyright (C) 2017 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * This is a test for the TCP_CORK and TCP_NODELAY socket options described
 * in the tcp(7) manual page. The server thread opens a socket, accepts
 * connection and then writes to in two separate writes of 100 bytes. If the
 * first write is not acknowleged for 100ms (because of "delayed ack"), the
 * second write is not sent (because of Naggle's algorithm). If the client
 * measures the amount of time it takes to get from 1 to 200 bytes, he will
 * see more than 100ms.
 * But if TCP_CORK is used - and correctly supported - the two writes will
 * be flushed immediately as TCP_CORK is released, and this will take much
 * less than 100ms. Similarly, when TCP_NODELAY is used, each write will be
 * set out immediately, and not be delayed by Naggle's algorithm.
 */

#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>

#include <cstdlib>
#include <thread>
#include <chrono>


// server() opens a listening TCP server on port 1234, accepts one connection,
// and measures how much time it takes to read all the data from the client.
static constexpr short LISTEN_TCP_PORT = 1234;
static int server(void)
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

    int fd = accept(listenfd, NULL, NULL);
    if (fd < 0) {
        perror("accept failed");
        close(listenfd);
        return 1;
    }
    // read() the expected 200 bytes. Measure how much time it takes between
    // accepting the connection and reading until getting all those bytes, and
    // return the number of milliseconds.
    auto before = std::chrono::high_resolution_clock::now();
    constexpr int bufsize = 4096;
    char buf[bufsize];
    int size = 0;
    int r;
    while (size < 200 && (r = read(fd, buf, bufsize)) > 0) {
        size += r;
    }
    assert(size == 200); // we should have read everything
    close(fd);
    close(listenfd);
    auto after = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            after-before).count();
}

static int client(bool cork, bool nodelay)
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

    if (cork) {
        int optval = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_CORK, &optval, sizeof(int)) < 0) {
            perror("setsockopt failed");
        }
    }
    if (nodelay) {
        int optval = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int)) < 0) {
            perror("setsockopt failed");
        }
    }

    // Make two small writes, of 100 bytes each. Sleep for 5ms between them,
    // hoping to give the kernel enough time to decide to send the first
    // 100 bytes as a separate packet, before finding it has another 100 bytes
    // to send.
    constexpr int bufsize = 100;
    char *buf = (char*) calloc(bufsize, 1);
    int wr = write(sock, buf, bufsize);
    assert(wr == bufsize); // smaller than socket buffer, so shouldn't fail...
    usleep(5000);
    wr = write(sock, buf, bufsize);
    assert(wr == bufsize);
    free(buf);

    if (cork) {
        int optval = 0;
        if (setsockopt(sock, IPPROTO_TCP, TCP_CORK, &optval, sizeof(int)) < 0) {
            perror("setsockopt failed");
        }
    }

    // Now sleep 500ms. The kernel should send the 200 bytes much sooner
    // than that (immediately with TCP_CORK, after the "delayed ack" timeout
    // (100ms) without TCP_CORK.
    usleep(500000);
    close(sock);
    return 0;
}

static bool test(bool cork, bool nodelay)
{
    int srv_result;
    std::thread t([&srv_result] { srv_result = server(); });
    sleep(1);
    int clnt_result = client(cork, nodelay);
    t.join();
    printf("test %d %d time (ms): %d \n", cork, nodelay, srv_result);
    if (cork || nodelay) {
        // If TCP_CORK is used, both batches of 100 bytes are sent as soon as
        // we release the TCP_CORK option, which takes about 5ms because of the
        // 5ms delay we have between the two batches. It can be a bit more than
        // 5ms because of measurement inaccuracy or context switch costs, but
        // must never grow significanlty above that, and certainly not to levels
        // of 100ms (which is what we'll get without TCP_CORK, see below).
        // FIXME: If the host preempts this guest for longer than 50ms, this
        // can confuse this test. I'm not sure how to solve this issue. Perhaps
        // should check steal time here and rerun the test?
        // In the meantime, do not run tests on extremely overcommitted hosts...
        return srv_result >= 4 && srv_result < 50 && clnt_result == 0;
    } else {
        // If TCP_CORK is not used, the second batch of 100 bytes sent by
        // client is delayed until the server does delayed ack on the first 100
        // bytes, i.e., by 100ms, so we expect the server to complete in exactly
        // 100ms. It can be a tiny bit more because of inaccuracies, but it cannot
        // be less (delayed ack cannot timeout sooner) and it cannot be as high
        // as 500ms - at 500ms the client closes the socket and flushes the data
        // immediately.
        return srv_result >= 99 && srv_result < 200 && clnt_result == 0;
    }
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
    report(test(false, false), "Without TCP_CORK, delayed ack comes into play");
    report(test(true, false), "With TCP_CORK, no nagle so no delayed ack");
    report(test(false, true), "With TCP_NODELAY, no nagle so no delayed ack");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails;
}
