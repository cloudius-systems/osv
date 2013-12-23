/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

//
// Same as an Echo server but more practical to validate correctness of
// recieved / transmitted data. Uses TCP.
//
#include <debug.hh>
#include <sched.hh>
#include <string>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static u8 hash_function(u8 * data, u32 len)
{
    u8 result = 0;
    for (u32 i=0; i<len; i++) {
        result ^= data[i];
    }
    return result;
}

#define dbg(...) tprintf_d("tst-tcp-hash-srv", __VA_ARGS__)

const int listen_port = 2500;
const int chunk_size = 1024;

class tcp_hash_connection {
public:
    tcp_hash_connection(int fd): _fd(fd) {
        dbg("server: creating thread to handle connection...");

        _conn = new sched::thread([&] { handle_connection(); },
                sched::thread::attr().detached());
        _conn->start();
    }

    // read in chunks and output hash
    void handle_connection() {
        u8* chunk = new u8[chunk_size];
        ssize_t total_bytes=0;
        u8 response = 0;
        bool ended = false;

        dbg("server: processing data...");
        ssize_t bytes = 1;
        int chunks_nr = 0;
        while ((bytes > 0) && (!ended)) {
            bytes = recv(_fd, chunk, chunk_size, 0);
            if ((bytes >= 3) &&
                (chunk[bytes-3] == 'E') && (chunk[bytes-2] == 'N') && (chunk[bytes-1] == 'D')) {
                bytes -= 3;
                ended = true;
            }

            if (bytes > 0) {
                total_bytes += bytes;
                response ^= hash_function(chunk, bytes);
                chunks_nr++;
            }
        }

        dbg("server: received %d bytes in %d chunks, computed hash is %d...",
            total_bytes, chunks_nr, response);

        send(_fd, &response, 1, 0);
        close(_fd);

        delete chunk;
        delete this;
    }

private:
    int _fd;
    sched::thread *_conn;
};

class tcp_hash_server {
public:

    tcp_hash_server(): _listen_fd(0) {}

    bool run() {
        int optval, error;
        struct sockaddr_in laddr;

        /* Create listening socket */
        dbg("server: creating listening socket...");
        _listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (_listen_fd < 0) {
            dbg("server: socket() failed!");
            return false;
        }

        /* Reuse bind address */
        optval = 1;
        error = setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval,
            sizeof(optval));
        if (error < 0) {
            dbg("server: setsockopt(SO_REUSEADDR) failed, error=%d", error);
            return false;
        }

        /* Bind */
        memset(&laddr, 0, sizeof(laddr));
        laddr.sin_family = AF_INET;
        inet_aton("0.0.0.0", &laddr.sin_addr);
        laddr.sin_port = htons(listen_port);

        if ( bind(_listen_fd, (struct sockaddr *) &laddr, sizeof(laddr)) < 0 ) {
            dbg("server: bind() failed!");
            return false;
        }

        /* Listen */
        dbg("listening on port %d...", listen_port);
        if ( listen(_listen_fd, 256) < 0 ) {
            dbg("server: listen() failed!");
            return false;
        }

        while (true) {
            int client_s = accept(_listen_fd, NULL, NULL);
            if (client_s < 0) {
                dbg("server: accept() failed!");
                return false;
            }

            // Create a new tcp hash connection
            new tcp_hash_connection(client_s);
        }

        // never reached
        return true;
    }
private:
    int _listen_fd;
};


int main(int argc, char* argv[])
{
    tcp_hash_server hs;
    bool rc = hs.run();

    return (rc ? 1 : 0);
}
