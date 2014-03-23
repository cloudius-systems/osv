#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "stat.hh"
#include <osv/device.h>
#include <osv/bio.h>
#include <osv/prex.h>
#include <osv/mempool.hh>

#define MB (1024*1024)
#define KB (1024)

static std::chrono::high_resolution_clock s_clock;

std::atomic<int> bio_inflights(0);
std::atomic<long> bytes_written(0);

static void bio_done(struct bio* bio)
{
    auto err = bio->bio_flags & BIO_ERROR;
    bytes_written += bio->bio_bcount;
    delete [] (char*) bio->bio_data;
    destroy_bio(bio);
    bio_inflights--;
    if (err) {
        printf("bio err!\n");
    }
}

void do_test_cycle(struct device *dev, int buf_size_pages, long max_offset)
{
    const std::chrono::seconds test_duration(10);
    const int buf_size = buf_size_pages * memory::page_size;

    long total = 0;
    long offset = 0;

    auto test_start = s_clock.now();
    auto end_at = test_start + test_duration;

    printf("Testing with %d page(s) buffers:\n", buf_size_pages);

    stat_printer _stat_printer(bytes_written, [] (float bytes_per_second) {
        printf("%.3f Mb/s\n", (float)bytes_per_second / MB);
    }, 1000);

    while (s_clock.now() < end_at) {
        auto bio = alloc_bio();
        bio_inflights++;
        bio->bio_cmd = BIO_WRITE;
        bio->bio_dev = dev;
        bio->bio_data = new char[buf_size];
        bio->bio_offset = offset;
        bio->bio_bcount = buf_size;
        bio->bio_caller1 = bio;
        bio->bio_done = bio_done;

        dev->driver->devops->strategy(bio);

        offset += buf_size;
        total += buf_size;

        if (max_offset != 0 && offset >= max_offset)
            offset = 0;
    }

    while (bio_inflights != 0) {
        usleep(2000);
    }

    auto test_end = s_clock.now();
    _stat_printer.stop();

    auto actual_test_duration = to_seconds(test_end - test_start);
    printf("Wrote %.3f MB in %.2f s = %.3f Mb/s\n", (double) total / MB,
            actual_test_duration, (double) total / MB / actual_test_duration);
}

int main(int argc, char const *argv[])
{
    struct device *dev;
    if (argc < 2) {
        printf("Usage: %s <dev-name> [max-write-offset] "
               "[buffer size in pages]\n", argv[0]);
        return 1;
    }

    if (device_open(argv[1], DO_RDWR, &dev)) {
        printf("open failed\n");
        return 1;
    }

    long max_offset = 0;
    if (argc > 2) {
        max_offset = atol(argv[2]);
    }

    long buffer_size_pages = 0;
    if (argc > 3) {
        buffer_size_pages = atol(argv[3]);
    }

    printf("bdev-write test offset limit: %ld byte(s)\n", max_offset);

    if (buffer_size_pages == 0) {
        do_test_cycle(dev, 32, max_offset);
        do_test_cycle(dev, 1, max_offset);
    } else {
        do_test_cycle(dev, buffer_size_pages, max_offset);
    }

    return 0;
}
