/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A forked process keeps more block requests in flight on one NVMe queue
// than the queue has slots.  The driver then reuses command ids with a
// higher "row", allocating the row on the first conflict from the
// submitting thread.  The completion thread looks bios up in that row from
// its own address space; if the row is in the forked process's
// copy-on-write heap arena, the completion thread reads and writes a
// different copy and the guest faults, or the requests never complete and a
// watchdog fails the test.
//
// Usage: tst-fork-nvme-cid.so <nvme device> [rounds] (default /dev/vblk1, 20).
// The first 64 MiB of the device are overwritten: give it a scratch disk.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-nvme-cid requires CONF_fork\n");
    return 0;
}

#else

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/prex.h>
#include <osv/sched.hh>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static void watchdog(int)
{
    static const char msg[] = "FAIL: block requests never completed\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

// Many writes in flight at once from one CPU, so they all go to one queue.
// The bios come from alloc_bio() (kernel heap) and carry no completion
// callback: the caller waits on each, so nothing the completion thread
// touches is in this process's heap except what the driver itself put there.
// The data buffer is never read back by the completion path (DMA only).
static int burst(struct device *dev, int n)
{
    const size_t len = 4096;
    static char buf[4096] __attribute__((aligned(4096)));
    std::vector<struct bio*> bios(n);
    for (int i = 0; i < n; i++) {
        auto bio = alloc_bio();
        bio->bio_cmd = BIO_WRITE;
        bio->bio_dev = dev;
        bio->bio_data = buf;
        bio->bio_offset = (off_t)i * len;
        bio->bio_bcount = len;
        bios[i] = bio;
        dev->driver->devops->strategy(bio);
    }
    int errors = 0;
    for (auto bio : bios) {
        errors += bio_wait(bio) != 0;
        destroy_bio(bio);
    }
    return errors;
}

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "vblk1";
    if (!strncmp(name, "/dev/", 5)) {
        name += 5;
    }
    int rounds = argc > 2 ? atoi(argv[2]) : 20;
    struct device *dev;
    if (device_open(name, DO_RDWR, &dev)) {
        printf("SKIP: cannot open %s\n", name);
        return 0;
    }
    signal(SIGALRM, watchdog);
    int failures = 0;
    for (int r = 0; r < rounds; r++) {
        pid_t pid = fork();
        if (pid == 0) {
            sched::thread::pin(sched::cpus[0]);
            // Fill the child's heap arena so its pages are its own copies.
            for (int i = 0; i < 256; i++) {
                memset(malloc(512), i, 512);
            }
            alarm(30);
            _exit(burst(dev, 1024) ? 2 : 0);
        }
        int status = 0;
        alarm(60);
        if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            printf("FAIL: round %d: child status 0x%x\n", r, status);
            failures++;
        }
        alarm(0);
        // The parent reuses the same command ids, through the same rows.
        sched::thread::pin(sched::cpus[0]);
        if (burst(dev, 1024)) {
            printf("FAIL: round %d: parent I/O error\n", r);
            failures++;
        }
    }
    device_close(dev);
    printf("%d rounds, %d failures\n", rounds, failures);
    if (failures == 0) {
        printf("PASS\n");
    }
    return failures ? 1 : 0;
}

#endif
