/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Checks that VFS passes O_NONBLOCK to devices as IO_NONBLOCK, and that
// before seeding /dev/random honors it while /dev/urandom ignores it.
// Uses device_create() and the random adaptor, so it is an internal-API test.

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

#include <iostream>
#include <string>

#include <osv/device.h>
#include <osv/vnode.h>
#include <dev/random/randomdev.h>
#include <dev/random/random_adaptors.h>

static_assert((IO_NONBLOCK & (IO_APPEND | IO_SYNC | IO_DIRECT)) == 0,
    "IO_NONBLOCK must not alias another IO_* flag");

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

// A device that records the IO_* flags of its last read or write.
static int seen_flags;
static int record_io(device*, uio* io, int flags)
{
    seen_flags = flags;
    io->uio_resid = 0;
    return 0;
}
static devops record_ops{no_open, no_close, record_io, record_io, no_ioctl, no_devctl};
static driver record_driver{"tst-ioflags", &record_ops};

static void test_device_flags(int type, const char* name)
{
    device* dev = device_create(&record_driver, name, type);
    report(dev != nullptr, std::string("device_create ") + name);
    if (!dev) {
        return;
    }
    std::string path = std::string("/dev/") + name;
    char buf[1] = {};
    // True if the call moved one byte and the device saw exactly "want".
    auto saw = [](ssize_t r, int want) {
        bool ok = r == 1 && seen_flags == want;
        seen_flags = -1;
        return ok;
    };
    seen_flags = -1;
    for (int mask = 0; mask < 32; ++mask) {
        int oflags = (mask & 1 ? O_NONBLOCK : 0) | (mask & 2 ? O_APPEND : 0)
            | (mask & 4 ? O_SYNC : 0) | (mask & 8 ? O_DSYNC : 0)
            | (mask & 16 ? O_DIRECT : 0);
        int rd = oflags & O_NONBLOCK ? IO_NONBLOCK : 0;
        int wr = rd | (oflags & O_APPEND ? IO_APPEND : 0)
            | (oflags & (O_SYNC | O_DSYNC) ? IO_SYNC : 0);
        // The positioned block-device write path does not apply O_APPEND.
        int pwr = type == D_BLK ? wr & ~IO_APPEND : wr;
        int fd = open(path.c_str(), O_RDWR | oflags);
        bool ok = fd >= 0
            && saw(read(fd, buf, 1), rd)
            && saw(pread(fd, buf, 1, 0), rd)
            && saw(write(fd, buf, 1), wr)
            && saw(pwrite(fd, buf, 1, 0), pwr);
        if (fd >= 0) {
            close(fd);
        }
        report(ok, path + " flag mask " + std::to_string(mask)
            + ": read/pread/write/pwrite pass the expected IO_* flags");
    }
    report(device_destroy(dev) == 0, std::string("device_destroy ") + name);
    report(open(path.c_str(), O_RDONLY) == -1, path + " cannot be opened after destroy");
}

// Stands in for the adaptor while it is unseeded. A blocking caller would
// sleep in the real randomdev_block() until seeded; the stub returns as if
// that sleep had ended, so only the flags the driver passed matter.
static int block_flags;
static int unseeded_block(int flags)
{
    block_flags = flags;
    return flags & O_NONBLOCK ? EWOULDBLOCK : 0;
}
static int stub_read(void* buf, int count)
{
    if (buf) {
        memset(buf, 'r', count);
    }
    return count;
}

static void test_unseeded(const char* path, bool nonblock_fails)
{
    std::string p = path;
    char buf[16];
    iovec iov{buf, sizeof(buf)};
    for (bool setfl : {false, true}) {
        // Nonblocking via open(), or via fcntl() after a blocking read.
        int fd = open(path, O_RDONLY | (setfl ? 0 : O_NONBLOCK));
        report(fd >= 0, "open " + p);
        if (fd < 0) {
            continue;
        }
        if (setfl) {
            block_flags = -1;
            report(read(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf) && block_flags == 0,
                p + " blocking read passes no O_NONBLOCK and returns data");
            report(fcntl(fd, F_SETFL, O_NONBLOCK) == 0, p + " F_SETFL O_NONBLOCK");
        }
        std::string how = setfl ? " (F_SETFL)" : " (open)";
        for (int op = 0; op < 3; ++op) {
            static const char* names[] = {"read", "pread", "readv"};
            memset(buf, 0, sizeof(buf));
            block_flags = -1;
            errno = 0;
            ssize_t r = op == 0 ? read(fd, buf, sizeof(buf))
                : op == 1 ? pread(fd, buf, sizeof(buf), 0)
                : readv(fd, &iov, 1);
            if (nonblock_fails) {
                report(r == -1 && errno == EAGAIN && block_flags == O_NONBLOCK,
                    p + " nonblocking " + names[op] + how + " fails with EAGAIN");
            } else {
                report(r == (ssize_t)sizeof(buf) && buf[0] == 'r' && block_flags == 0,
                    p + " nonblocking " + names[op] + how + " returns data, not EAGAIN");
            }
        }
        close(fd);
    }
}

int main()
{
    test_device_flags(D_CHR, "tst-iof-c");
    test_device_flags(D_BLK, "tst-iof-b");

    // The real adaptor is normally seeded before main() runs, and taking it
    // back to unseeded would mean disabling entropy harvesting. Swap in a
    // stub that answers as an unseeded adaptor does, then restore it.
    // Only the /dev/random and /dev/urandom read path uses this pointer;
    // any other reader of those devices meanwhile gets the stub's bytes.
    struct random_adaptor stub{};
    stub.ident = "tst-random-vfs unseeded stub";
    stub.block = unseeded_block;
    stub.read = stub_read;
    struct random_adaptor* real = random_adaptor;
    random_adaptor = &stub;
    test_unseeded("/dev/random", true);
    test_unseeded("/dev/urandom", false);
    random_adaptor = real;

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails;
}
