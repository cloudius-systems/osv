/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises inotify(2): watch a directory and observe create/delete/move
// events.  Built and run as part of the OSv test image.

#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <iostream>

// Read one event at a time.  Because a single read() may return several packed
// events (e.g. a rename yields MOVED_FROM+MOVED_TO), buffer the surplus and hand
// them out one call at a time.  Blocks up to 2s for the first batch.
static char ev_buf[4096];
static ssize_t ev_have = 0;
static ssize_t ev_off = 0;

static uint32_t read_event(int fd, std::string& name)
{
    if (ev_off >= ev_have) {
        struct pollfd pfd { fd, POLLIN, 0 };
        assert(poll(&pfd, 1, 2000) == 1);
        ev_have = read(fd, ev_buf, sizeof(ev_buf));
        ev_off = 0;
        assert(ev_have >= (ssize_t)sizeof(inotify_event));
    }
    auto* ev = reinterpret_cast<inotify_event*>(ev_buf + ev_off);
    name = ev->len ? std::string(ev->name) : std::string();
    ev_off += sizeof(inotify_event) + ev->len;
    return ev->mask;
}

int main()
{
    std::cerr << "Running inotify tests\n";

    // Work in a fresh writable directory under /tmp (ramfs).
    const char* dir = "/tmp/inotify-test";
    mkdir(dir, 0777);

    int ifd = inotify_init1(IN_NONBLOCK);
    assert(ifd >= 0);

    int wd = inotify_add_watch(ifd, dir, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    assert(wd >= 0);

    // No events yet: nonblocking read returns EAGAIN.
    char tmp[64];
    assert(read(ifd, tmp, sizeof(tmp)) == -1 && errno == EAGAIN);

    // Create a file -> IN_CREATE with its name.
    std::string p = std::string(dir) + "/file1";
    int f = open(p.c_str(), O_CREAT | O_WRONLY, 0644);
    assert(f >= 0);
    close(f);

    std::string name;
    uint32_t mask = read_event(ifd, name);
    assert((mask & IN_CREATE) && name == "file1");

    // Rename it -> IN_MOVED_FROM (old name) then IN_MOVED_TO (new name).
    std::string p2 = std::string(dir) + "/file2";
    assert(rename(p.c_str(), p2.c_str()) == 0);
    mask = read_event(ifd, name);
    assert((mask & IN_MOVED_FROM) && name == "file1");
    mask = read_event(ifd, name);
    assert((mask & IN_MOVED_TO) && name == "file2");

    // Delete it -> IN_DELETE.
    assert(unlink(p2.c_str()) == 0);
    mask = read_event(ifd, name);
    assert((mask & IN_DELETE) && name == "file2");

    // Create a subdirectory -> IN_CREATE | IN_ISDIR.
    std::string sub = std::string(dir) + "/subdir";
    assert(mkdir(sub.c_str(), 0777) == 0);
    mask = read_event(ifd, name);
    assert((mask & IN_CREATE) && (mask & IN_ISDIR) && name == "subdir");
    rmdir(sub.c_str());
    mask = read_event(ifd, name);
    assert((mask & IN_DELETE) && (mask & IN_ISDIR));

    // Removing the watch queues IN_IGNORED and rejects a bogus wd.
    assert(inotify_rm_watch(ifd, wd) == 0);
    errno = 0;
    assert(inotify_rm_watch(ifd, 9999) == -1 && errno == EINVAL);

    // Bad init flags rejected.
    errno = 0;
    assert(inotify_init1(0x40) == -1 && errno == EINVAL);

    close(ifd);
    std::cerr << "inotify tests PASSED\n";
    return 0;
}
