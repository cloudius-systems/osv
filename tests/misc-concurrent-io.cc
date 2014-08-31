/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Use this benchmark to measure root file system performance when doing
// parallel I/O into the same file.
// For example, ZFS allows parallel reads, or even parallel writes into
// different ranges, on the same file.
//
// To run this benchmark on Linux:
//    g++ -pthread -std=c++11 tests/misc-concurrent-io.cc
//    ./a.out setup
//    ./a.out <operation>
// To run on OSv:
//    make image=tests
//    scripts/run.py -e "tests/misc-concurrent-io.so setup"
//    scripts/run.py -e "tests/misc-concurrent-io.so <operation>"

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
#include <memory>

using Clock = std::chrono::high_resolution_clock;

static constexpr int num_threads = 10;
static unsigned file_range_length = 10 * 1024 * 1024;
static unsigned file_size = num_threads * file_range_length;

static Clock s_clock;
static std::chrono::time_point<Clock> test_start;
static float min_secs = FLT_MAX;
static float max_secs = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct file_range {
    int thread_id;
    int fd;
    off_t offset;
    ssize_t length;
};

struct result_data {
    std::chrono::time_point<Clock> start;
    std::chrono::time_point<Clock> end;
    struct file_range *file_range;
};

template<typename T>
static float to_seconds(T duration)
{
    return std::chrono::duration<float>(duration).count();
}

static void random_memset(unsigned char *s, ssize_t n)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
        perror("open");
        _exit(-1);
    }
    assert(read(fd, s, n) == n);
    if (close(fd) == -1) {
        perror("close");
        _exit(-1);
    }
}

static inline void try_to_assign_min_max_secs(float to_min_secs,
    float to_max_secs)
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

static void process_result(struct result_data &result)
{
    auto test_start_to_start = to_seconds(result.start - test_start);
    auto test_start_to_end = to_seconds(result.end - test_start);
    try_to_assign_min_max_secs(test_start_to_start, test_start_to_end);

    printf("Thread %d, %ld bytes operated from offset %d, "
        "duration <%.2fms : %.2fms>\n",
        result.file_range->thread_id,
        result.file_range->length,
        result.file_range->offset,
        test_start_to_start * 1000,
        test_start_to_end * 1000);
}

// Concurrently read the same file.
static void *read_function(void *arg)
{
    struct file_range *file_range = (struct file_range *) arg;
    int fd = file_range->fd;
    ssize_t length = file_range->length;
    off_t offset = file_range->offset;
    std::unique_ptr<unsigned char []> buf(new unsigned char[length]);

    auto start = s_clock.now();
    auto ret = pread(fd, buf.get(), length, offset);
    auto end = s_clock.now();
    assert(ret == length);

    struct result_data result({ start, end, file_range });
    process_result(result);

    return nullptr;
}

// Concurrently seek and read the same file.
static void *seek_n_read_function(void *arg)
{
    struct file_range *file_range = (struct file_range *) arg;
    int fd;
    ssize_t length = file_range->length;
    off_t offset = file_range->offset;
    std::unique_ptr<unsigned char []> buf(new unsigned char[length]);

    // needed to avoid file offset sharing between threads.
    fd = dup(file_range->fd);
    assert(fd != -1);

    auto start = s_clock.now();
    // purpose is to check contention on lseek when parallel threads are
    // doing I/O on the same underlying VFS node.
    auto new_offset = lseek(fd, offset, SEEK_SET);
    auto ret = read(fd, buf.get(), length);
    auto end = s_clock.now();
    assert(new_offset == offset);
    assert(ret == length);

    struct result_data result({ start, end, file_range });
    process_result(result);

    return nullptr;
}

// Concurrently write the same file.
static void *write_function(void *arg)
{
    struct file_range *file_range = (struct file_range *) arg;
    int fd = file_range->fd;
    ssize_t length = file_range->length;
    off_t offset = file_range->offset;
    std::unique_ptr<unsigned char []> buf(new unsigned char[length]);

    auto start = s_clock.now();
    auto ret = pwrite(fd, buf.get(), length, offset);
    auto end = s_clock.now();
    assert(ret == length);

    struct result_data result({ start, end, file_range });
    process_result(result);

    return nullptr;
}

static inline void do_run_test(int fd, bool diff_range, ssize_t range_length,
    void *(*function)(void *))
{
    pthread_t thread_id[num_threads];
    struct file_range file_ranges[num_threads];

    test_start = s_clock.now();
    for (int i = 0; i < num_threads; i++) {
        file_ranges[i].thread_id = i;
        file_ranges[i].fd = fd;
        file_ranges[i].offset = (diff_range) ? i * file_range_length : 0;
        file_ranges[i].length = range_length;

        auto ret = pthread_create(&thread_id[i], nullptr, function,
            (void *)&file_ranges[i]);
        assert(ret == 0);
    }

    for (int i = 0; i < num_threads; i++) {
        assert(pthread_join(thread_id[i], nullptr) == 0);
    }
}

static void run_test(const char *filepath, int oflags, bool diff_range,
    ssize_t range_length, void *(*function)(void *))
{
    int fd = open(filepath, oflags);
    if (fd == -1) {
        perror("open");
        _exit(-1);
    }

    do_run_test(fd, diff_range, range_length, function);

    printf("Duration <%.2fms : %.2fms> = %.2fms\n",
        min_secs * 1000, max_secs * 1000,
        (max_secs - min_secs) * 1000);

    if (close(fd) == -1) {
        perror("close");
        _exit(-1);
    }
}

static void usage(char *app)
{
    fprintf(stderr, "usage:\n%s op [file_path] [file_range_length]\n\n"
        "Help: '%s setup' is required to run the other operations.\n"
        "Note: file_range_length parameter will be multiplied by 1MiB.\n",
        app, app);
    _exit(-1);
}

int main(int argc, char **argv)
{
    char default_filepath[64] = "/tst-concurrent-io.tmp";
    const char *filepath;

    if (argc < 2 || argc > 4) {
        usage(argv[0]);
    }

    if (argc >= 3) {
        filepath = argv[2];
    } else {
        filepath = default_filepath;
    }

    if (argc >= 4) {
        auto ret = atoi(argv[3]);
        assert(ret > 0);
        file_range_length = ret * 1024 * 1024;
        file_size = num_threads * file_range_length;
    }

    printf("Threads: %d, file: %s, file size: %u, file range length: %u\n\n",
        num_threads, filepath, file_size, file_range_length);

    if (!strcmp(argv[1], "setup")) {
        int fd = open(filepath, O_CREAT | O_WRONLY);
        if (fd == -1) {
            perror("open");
            return -1;
        }
        assert(ftruncate(fd, file_size) == 0);

        std::unique_ptr<unsigned char []> buf(new unsigned char[file_size]);
        random_memset(buf.get(), file_size);
        auto ret = write(fd, buf.get(), file_size);
        assert(ret == file_size);

        printf("%s: setup phase finished successfully!\n", argv[0]);

        if (close(fd) == -1) {
            perror("close");
            return -1;
        }
    } else if (!strcmp(argv[1], "read-diff-ranges")) {
        run_test(filepath, O_RDONLY | O_SYNC, true, file_range_length,
            read_function);
    } else if (!strcmp(argv[1], "read-same-range")) {
        run_test(filepath, O_RDONLY | O_SYNC, false, file_size,
            read_function);
    } else if (!strcmp(argv[1], "seeknread-diff-ranges")) {
        run_test(filepath, O_RDONLY | O_SYNC, true, file_range_length,
            seek_n_read_function);
    } else if (!strcmp(argv[1], "seeknread-same-range")) {
        run_test(filepath, O_RDONLY | O_SYNC, false, file_size,
            seek_n_read_function);
    } else if (!strcmp(argv[1], "write-diff-ranges")) {
        run_test(filepath, O_WRONLY | O_SYNC, true, file_range_length,
            write_function);
    } else if (!strcmp(argv[1], "write-same-range")) {
        run_test(filepath, O_WRONLY | O_SYNC, false, file_size,
            write_function);
    } else {
        usage(argv[0]);
    }

    return 0;
}
