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

static uint32_t read_event(int fd, std::string& name, uint32_t* cookie = nullptr,
                           int* wd = nullptr)
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
    if (cookie) {
        *cookie = ev->cookie;
    }
    if (wd) {
        *wd = ev->wd;
    }
    ev_off += sizeof(inotify_event) + ev->len;
    return ev->mask;
}

// Assert that nothing more is queued.
static void expect_none(int fd)
{
    char tmp[64];
    assert(ev_off >= ev_have);
    assert(read(fd, tmp, sizeof(tmp)) == -1 && errno == EAGAIN);
}

// Expect exactly one event, with this mask, name and watch.
static void expect_event(int fd, uint32_t want, const char* want_name, int want_wd)
{
    std::string name;
    int wd = -1;
    uint32_t mask = read_event(fd, name, nullptr, &wd);
    if (mask != want || name != want_name || wd != want_wd) {
        std::cerr << "got mask 0x" << std::hex << mask << std::dec << " name '" << name
                  << "' wd " << wd << ", want 0x" << std::hex << want << std::dec
                  << " '" << want_name << "' wd " << want_wd << "\n";
        assert(false);
    }
}

static void touch(const char* path)
{
    int f = open(path, O_CREAT | O_WRONLY, 0644);
    assert(f >= 0);
    close(f);
}

// Watched paths are normalized like the VFS normalizes the paths it reports,
// and a watch on an object itself reports only IN_DELETE_SELF (then
// IN_IGNORED) and IN_MOVE_SELF.  Runs inside `dir`, using relative paths.
static void test_paths_and_self(const char* dir)
{
    assert(chdir(dir) == 0);
    int ifd = inotify_init1(IN_NONBLOCK);
    assert(ifd >= 0);
    const uint32_t all = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
                         IN_DELETE_SELF | IN_MOVE_SELF;

    // A watch on "." after chdir().
    int wd = inotify_add_watch(ifd, ".", all);
    assert(wd >= 0);
    touch("f1");
    expect_event(ifd, IN_CREATE, "f1", wd);
    expect_none(ifd);
    assert(inotify_rm_watch(ifd, wd) == 0);
    expect_event(ifd, IN_IGNORED, "", wd);

    // A watch through "..", and mkdir() with a trailing slash.
    assert(mkdir("d2", 0777) == 0);
    wd = inotify_add_watch(ifd, "d2/../d2/", all);
    assert(wd >= 0);
    touch("d2/f");
    expect_event(ifd, IN_CREATE, "f", wd);
    assert(mkdir("d2/sub/", 0777) == 0);
    expect_event(ifd, IN_CREATE | IN_ISDIR, "sub", wd);

    // mknod(), symlink() and link() create entries too.  A filesystem without
    // hard links (OSv's ramfs) fails link() with EPERM instead.
    assert(mknod("d2/node", S_IFREG | 0644, 0) == 0);
    expect_event(ifd, IN_CREATE, "node", wd);
    assert(symlink("node", "d2/sym") == 0);
    expect_event(ifd, IN_CREATE, "sym", wd);
    if (link("d2/node", "d2/hard") == 0) {
        expect_event(ifd, IN_CREATE, "hard", wd);
        assert(unlink("d2/hard") == 0);
        expect_event(ifd, IN_DELETE, "hard", wd);
    } else {
        assert(errno == EPERM);
    }
    expect_none(ifd);
    assert(inotify_rm_watch(ifd, wd) == 0);
    expect_event(ifd, IN_IGNORED, "", wd);
    assert(unlink("d2/sym") == 0 && unlink("d2/node") == 0 && unlink("d2/f") == 0);
    assert(rmdir("d2/sub") == 0 && rmdir("d2") == 0);

    // A watch on a file itself: unlink() gives IN_DELETE_SELF, then
    // IN_IGNORED, and the watch is gone.
    touch("f3");
    wd = inotify_add_watch(ifd, "f3", all);
    assert(wd >= 0);
    assert(unlink("f3") == 0);
    expect_event(ifd, IN_DELETE_SELF, "", wd);
    expect_event(ifd, IN_IGNORED, "", wd);
    expect_none(ifd);
    errno = 0;
    assert(inotify_rm_watch(ifd, wd) == -1 && errno == EINVAL);

    // rename() of a watched file gives IN_MOVE_SELF only.
    touch("f4");
    wd = inotify_add_watch(ifd, "f4", all);
    assert(wd >= 0);
    assert(rename("f4", "f4b") == 0);
    expect_event(ifd, IN_MOVE_SELF, "", wd);
    expect_none(ifd);
    assert(inotify_rm_watch(ifd, wd) == 0);
    expect_event(ifd, IN_IGNORED, "", wd);

    // A rename that replaces a watched file deletes it.
    touch("f4");
    wd = inotify_add_watch(ifd, "f4", all);
    assert(wd >= 0);
    assert(rename("f4b", "f4") == 0);
    expect_event(ifd, IN_DELETE_SELF, "", wd);
    expect_event(ifd, IN_IGNORED, "", wd);
    assert(unlink("f4") == 0 && unlink("f1") == 0);
    expect_none(ifd);

    // A self-event the watch did not ask for is not delivered, but a deleted
    // object's watch still ends with IN_IGNORED.
    touch("f5");
    wd = inotify_add_watch(ifd, "f5", IN_CREATE);
    assert(wd >= 0);
    assert(unlink("f5") == 0);
    expect_event(ifd, IN_IGNORED, "", wd);
    expect_none(ifd);

    // IN_ONESHOT drops the watch, with IN_IGNORED, after its first event.
    wd = inotify_add_watch(ifd, ".", IN_CREATE | IN_ONESHOT);
    assert(wd >= 0);
    touch("f6");
    expect_event(ifd, IN_CREATE, "f6", wd);
    expect_event(ifd, IN_IGNORED, "", wd);
    assert(unlink("f6") == 0);
    expect_none(ifd);

    // An empty mask is EINVAL; IN_MASK_CREATE on a watched path is EEXIST.
    errno = 0;
    assert(inotify_add_watch(ifd, ".", 0) == -1 && errno == EINVAL);
    wd = inotify_add_watch(ifd, ".", IN_CREATE);
    assert(wd >= 0);
    errno = 0;
    assert(inotify_add_watch(ifd, ".", IN_DELETE | IN_MASK_CREATE) == -1 && errno == EEXIST);
    assert(inotify_rm_watch(ifd, wd) == 0);
    expect_event(ifd, IN_IGNORED, "", wd);

    close(ifd);
    assert(chdir("/") == 0);
}

int main()
{
    std::cerr << "Running inotify tests\n";

    // Work in a fresh, per-run-unique writable directory under /tmp (ramfs) so
    // leftovers from a previous run cannot suppress the events we assert on.
    char dirbuf[64];
    snprintf(dirbuf, sizeof(dirbuf), "/tmp/inotify-test-%d", (int)getpid());
    const char* dir = dirbuf;
    assert(mkdir(dir, 0777) == 0);

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

    // Rename it -> IN_MOVED_FROM (old name) then IN_MOVED_TO (new name),
    // sharing a single non-zero cookie so readers can correlate the pair.
    std::string p2 = std::string(dir) + "/file2";
    assert(rename(p.c_str(), p2.c_str()) == 0);
    uint32_t c_from = 0, c_to = 0;
    mask = read_event(ifd, name, &c_from);
    assert((mask & IN_MOVED_FROM) && name == "file1");
    mask = read_event(ifd, name, &c_to);
    assert((mask & IN_MOVED_TO) && name == "file2");
    assert(c_from != 0 && c_from == c_to);

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
    expect_event(ifd, IN_IGNORED, "", wd);
    expect_none(ifd);
    errno = 0;
    assert(inotify_rm_watch(ifd, 9999) == -1 && errno == EINVAL);

    // Bad init flags rejected.
    errno = 0;
    assert(inotify_init1(0x40) == -1 && errno == EINVAL);

    // A watch on a nonexistent path fails with ENOENT (Linux semantics).
    int ifd2 = inotify_init1(IN_NONBLOCK);
    assert(ifd2 >= 0);
    std::string nope = std::string(dir) + "/does-not-exist";
    errno = 0;
    assert(inotify_add_watch(ifd2, nope.c_str(), IN_CREATE) == -1 && errno == ENOENT);
    close(ifd2);

    test_paths_and_self(dir);

    assert(rmdir(dir) == 0);
    close(ifd);
    std::cerr << "inotify tests PASSED\n";
    return 0;
}
