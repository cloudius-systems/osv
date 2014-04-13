/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <chrono>

#define LISTEN_PORT 7777

using _clock = std::chrono::high_resolution_clock;

int main(int argc, char**argv)
{
    auto listen_s = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_s < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in laddr = {};
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);
    laddr.sin_port = htons(LISTEN_PORT);

    if (bind(listen_s, (struct sockaddr *) &laddr, sizeof(laddr)) < 0) {
        perror("bind");
        return -1;
    }

    if (listen(listen_s, 1) < 0) {
        perror("listen");
        return -1;
    }

    printf("listening...\n");

    int client_s;
    if ((client_s = accept(listen_s, NULL, NULL)) < 0) {
        perror("accept");
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    close(client_s);

    close(listen_s);
}
