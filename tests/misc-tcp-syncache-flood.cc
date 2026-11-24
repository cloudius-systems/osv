/*
 * Copyright (c) 2026 Greg Burd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Guest half of scripts/tests/test_net.py's tcp_syncache_flood test.
//
// It overflows the listen queue of a listener on port 7777 with handshakes
// made over loopback, so that their final ACKs fail to create a socket in
// syncache_expand().  Loopback input runs on the netisr thread, so a SYN
// cache bucket lock left held there blocks any other thread that needs that
// bucket.  Then it serves port 7778, answering each connection with "ok";
// the host connects to 7778 through the NIC, whose receive thread is not the
// netisr thread, and each of those connections hashes to some SYN cache
// bucket.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static int listen_on(int port, int backlog)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (s < 0 || bind(s, (sockaddr*)&a, sizeof(a)) || listen(s, backlog)) {
        perror("listen");
        exit(1);
    }
    return s;
}

int main(int argc, char** argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 8000;
    listen_on(7777, 1);
    int served = listen_on(7778, 128);

    // Nothing is accepted on 7777, so once its queue holds two sockets
    // every later handshake fails in sonewconn().  Pace the connects so
    // that the netisr queue does not drop the SYNs.
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(7777);
    inet_aton("127.0.0.1", &a.sin_addr);
    for (int i = 0; i < n; i++) {
        int c = socket(AF_INET, SOCK_STREAM, 0);
        fcntl(c, F_SETFL, O_NONBLOCK);
        connect(c, (sockaddr*)&a, sizeof(a));
        if (i % 16 == 15) {
            usleep(1000);
        }
    }
    sleep(1);
    printf("flooded %d\n", n);
    fflush(stdout);

    for (;;) {
        int c = accept(served, nullptr, nullptr);
        if (c < 0) {
            continue;
        }
        send(c, "ok", 2, 0);
        close(c);
    }
}
