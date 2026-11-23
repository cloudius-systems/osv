/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// listen() must honour the application's backlog.  A server that asks for
// listen(fd, 1000) and has not accepted yet must queue at least 900 completed
// connections.  Past the queue limit the stack drops the client's final ACK
// and keeps the SYN cache entry, so the client believes it is connected but
// the server cannot accept it until the SYN-ACK is retransmitted 3 s later.
// So both ends are checked: each client sees its connect complete, and the
// server accepts all of them within 1 s.  On the current tree the server
// accepts 193.

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>
#include <vector>
#include <algorithm>

static constexpr int backlog = 1000;
static constexpr int nconn = 900;

int main()
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        listen(ls, backlog) != 0) {
        printf("FAIL: listen setup: errno %d\n", errno);
        return 1;
    }
    socklen_t alen = sizeof(a);
    getsockname(ls, (struct sockaddr *)&a, &alen);

    // Connect in batches of 50 and let each batch's handshakes finish before
    // the next: a burst of 900 SYNs would overflow the loopback input queue
    // (256 packets) and lose packets for a reason that has nothing to do with
    // the listen queue.
    std::vector<int> fds;
    int done = 0, failed = 0;
    for (int base = 0; base < nconn; base += 50) {
        int n = std::min(50, nconn - base);
        std::vector<struct pollfd> p(n);
        for (int i = 0; i < n; i++) {
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) {
                printf("FAIL: socket %d: errno %d\n", base + i, errno);
                return 1;
            }
            fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
            if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0 &&
                errno != EINPROGRESS) {
                printf("FAIL: connect %d: errno %d\n", base + i, errno);
                return 1;
            }
            fds.push_back(s);
            p[i] = {s, POLLOUT, 0};
        }
        // Up to 500 ms per batch: plenty on loopback, and short of the 1 s
        // SYN retransmit.
        int settled = 0;
        for (int round = 0; round < 50 && settled < n; round++) {
            poll(p.data(), n, 10);
            for (auto &e : p) {
                if (e.fd < 0 || !e.revents) {
                    continue;
                }
                int err = 0;
                socklen_t el = sizeof(err);
                getsockopt(e.fd, SOL_SOCKET, SO_ERROR, &err, &el);
                e.fd = -1;
                settled++;
                (err == 0 ? done : failed)++;
            }
        }
    }
    printf("listen(%d): %d of %d connects completed, %d failed, %d pending\n",
           backlog, done, nconn, failed, nconn - done - failed);

    // Accept for 1 s: everything the listen queue holds, none of what waits
    // for the 3 s SYN-ACK retransmit.
    fcntl(ls, F_SETFL, fcntl(ls, F_GETFL) | O_NONBLOCK);
    std::vector<int> acc;
    struct pollfd lp = {ls, POLLIN, 0};
    for (int round = 0; round < 100 && (int)acc.size() < nconn; round++) {
        int c;
        while ((c = accept(ls, nullptr, nullptr)) >= 0) {
            acc.push_back(c);
        }
        poll(&lp, 1, 10);
    }
    printf("accepted %zu of %d within 1 s\n", acc.size(), nconn);

    int failures = 0;
    bool ok = done >= nconn;
    printf("%s: %d clients complete the handshake with listen(fd, %d)\n",
           ok ? "PASS" : "FAIL", nconn, backlog);
    failures += !ok;
    ok = (int)acc.size() >= nconn;
    printf("%s: the server accepts all %d within 1 s\n",
           ok ? "PASS" : "FAIL", nconn);
    failures += !ok;
    printf("SUMMARY: %d failures\n", failures);
    fflush(stdout);

    // Drain the queue before closing the listener (connections past the
    // limit arrive on later SYN-ACK retransmits): closing a listener that
    // still holds unaccepted connections is a separate problem.
    for (int round = 0; round < 1000 && (int)acc.size() < nconn; round++) {
        int c;
        while ((c = accept(ls, nullptr, nullptr)) >= 0) {
            acc.push_back(c);
        }
        poll(&lp, 1, 10);
    }
    for (int s : acc) {
        close(s);
    }
    for (int s : fds) {
        close(s);
    }
    close(ls);
    return failures ? 1 : 0;
}
