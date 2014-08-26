/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Use this benchmark to measure root file system performance when doing
// parallel I/O into different ranges from the same file.
// For example, ZFS allows parallel reads, or even parallel writes into
// different ranges, on the same file.
//
// To run this benchmark on Linux:
//    g++ -pthread -std=c++11 tests/misc-concurrent-io.cc
//    ./a.out create
//    ./a.out write
//    ./a.out read
// To run on OSv:
//    make image=tests
//    scripts/run.py -e "tests/misc-concurrent-io.so create"
//    scripts/run.py -e "tests/misc-concurrent-io.so write"
//    scripts/run.py -e "tests/misc-concurrent-io.so read"

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <float.h>
#include <chrono>

using Clock = std::chrono::high_resolution_clock;

static constexpr int num_threads = 10;
static constexpr unsigned file_range_length = 10 * 1024 * 1024;
static constexpr unsigned file_size = num_threads * file_range_length;

static Clock s_clock;
static std::chrono::time_point<Clock> test_start;
static char *buf;
static float min_secs = FLT_MAX;
static float max_secs = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct file_range {
    int id;
    int fd;
    off_t offset;
};

template<typename T>
static float to_seconds(T duration)
{
    return std::chrono::duration<float>(duration).count();
}

static void try_to_assign_min_max_secs(float to_min_secs, float to_max_secs)
{
    pthread_mutex_lock(&mutex);
    if (to_min_secs < min_secs) {
        min_secs = to_min_secs;
    }

    if (to_max_secs > max_secs) {
        max_secs = to_max_secs;
    }
    pthread_mutex_unlock(&mutex);
}

// Concurrently read from different file ranges.
static void *read_function(void *arg)
{
    struct file_range *file_range = (struct file_range *) arg;
    const int fd = file_range->fd;
    const off_t offset = file_range->offset;

    auto start = s_clock.now();
    auto ret = pread(fd, buf, file_range_length, offset);
    auto end = s_clock.now();
    assert(ret == file_range_length);

    auto test_start_to_start = to_seconds(start - test_start);
    auto test_start_to_end = to_seconds(end - test_start);
    try_to_assign_min_max_secs(test_start_to_start, test_start_to_end);

#if 1
    printf("Thread %d, %d bytes read from offset %d, "
        "Duration %.2f ms : %.2f ms.\n",
        file_range->id, file_range_length, offset,
        test_start_to_start * 1000,
        test_start_to_end * 1000);
#endif

    return nullptr;
}

// Concurrently read from the same file range.
static void *read2_function(void *arg)
{
    struct file_range *file_range = (struct file_range *) arg;
    const int fd = file_range->fd;
    const off_t offset = 0; // ignore file_range->offset.

    auto start = s_clock.now();
    auto ret = pread(fd, buf, file_size, offset);
    auto end = s_clock.now();
    assert(ret == file_size);

    auto test_start_to_start = to_seconds(start - test_start);
    auto test_start_to_end = to_seconds(end - test_start);
    try_to_assign_min_max_secs(test_start_to_start, test_start_to_end);

#if 1
    printf("Thread %d, %d bytes read from offset %d, "
        "Duration %.2f ms : %.2f ms.\n",
        file_range->id, file_size, offset,
        test_start_to_start * 1000,
        test_start_to_end * 1000);
#endif

    return nullptr;
}

// Concurrently write into different file ranges.
static void *write_function(void *arg)
{
    struct file_range *file_range = (struct file_range *) arg;
    const int fd = file_range->fd;
    const off_t offset = file_range->offset;

    auto start = s_clock.now();
    auto ret = pwrite(fd, buf, file_range_length, offset);
    auto end = s_clock.now();
    assert(ret == file_range_length);

    auto test_start_to_start = to_seconds(start - test_start);
    auto test_start_to_end = to_seconds(end - test_start);
    try_to_assign_min_max_secs(test_start_to_start, test_start_to_end);

#if 1
    printf("Thread %d, %d bytes written from offset %d, "
        "Duration %.2f ms : %.2f ms.\n",
        file_range->id, file_range_length, offset,
        test_start_to_start * 1000,
        test_start_to_end * 1000);
#endif

    return nullptr;
}

static void run_test(int fd, void *(*function)(void *))
{
    pthread_t thread_id[num_threads];
    file_range file_ranges[num_threads];

    test_start = s_clock.now();
    for (int i = 0; i < num_threads; i++) {
        file_ranges[i].id = i;
        file_ranges[i].fd = fd;
        file_ranges[i].offset = i * file_range_length;

        pthread_create(&thread_id[i], nullptr, function,
            (void *)&file_ranges[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(thread_id[i], nullptr);
    }
}

static void usage(char *app)
{
    fprintf(stderr, "usage: %s <create|read|write>\n", app);
}

int main(int argc, char **argv)
{
    char fname[64] = "/tst-concurrent-io.tmp";

    if (argc != 2) {
        usage(argv[0]);
        return -1;
    }

    if (!strcmp(argv[1], "create")) {
        int oflags = O_CREAT | O_WRONLY | O_DIRECT;
        int fd = open(fname, oflags);
        if (fd == -1) {
            perror("open");
            return -1;
        }
        assert(ftruncate(fd, file_size) == 0);

        buf = new char[file_size];
        memset(buf, 0xAB, file_size);
        auto ret = pwrite(fd, buf, file_size, 0);
        assert(ret == file_size);
        delete buf;

        printf("%s: creation phase finished successfully!\n", argv[0]);

        close(fd);
    } else if (!strcmp(argv[1], "read")) {
        int oflags = O_RDONLY | O_SYNC;
        int fd = open(fname, oflags);
        if (fd == -1) {
            perror("open");
            return -1;
        }

        buf = new char[file_range_length];
        run_test(fd, read_function);
        delete buf;
        printf("%s: read: Duration %.2f ms : %.2f ms = %.2fms\n",
            argv[0], min_secs * 1000, max_secs * 1000,
            (max_secs - min_secs) * 1000);

        close(fd);
    } else if (!strcmp(argv[1], "read2")) {
        int oflags = O_RDONLY | O_SYNC;
        int fd = open(fname, oflags);
        if (fd == -1) {
            perror("open");
            return -1;
        }

        buf = new char[file_size];
        run_test(fd, read2_function);
        delete buf;
        printf("%s: read2: Duration %.2f ms : %.2f ms = %.2fms\n",
            argv[0], min_secs * 1000, max_secs * 1000,
            (max_secs - min_secs) * 1000);

        close(fd);
    } else if (!strcmp(argv[1], "write")) {
        int oflags = O_WRONLY | O_SYNC;
        int fd = open(fname, oflags);
        if (fd == -1) {
            perror("open");
            return -1;
        }

        buf = new char[file_range_length];
        memset(buf, 0xBA, file_range_length);
        run_test(fd, write_function);
        delete buf;
        printf("%s: write: Duration %.2f ms : %.2f ms = %.2fms\n",
            argv[0], min_secs * 1000, max_secs * 1000,
            (max_secs - min_secs) * 1000);

        close(fd);
    } else {
        usage(argv[0]);
        return -1;
    }

    return 0;
}
