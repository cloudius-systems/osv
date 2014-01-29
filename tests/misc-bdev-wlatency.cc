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
#include <osv/condvar.h>
#include <osv/mempool.hh>
#include <osv/clock.hh>

#define MB (1024*1024)
#define KB (1024)

void *bio_buffer;
unsigned long *bio_clock;

std::vector<unsigned long> completions;

condvar wait_bio;
mutex bio_mutex;

static void bio_done(struct bio* bio)
{
    WITH_LOCK(bio_mutex) {
        auto err = bio->bio_flags & BIO_ERROR;
        if (err) {
            printf("bio err!\n");
            exit(1);
        }

        auto now = clock::get()->time();
        unsigned long delta = now - *bio_clock;
        completions.push_back(delta);
        wait_bio.wake_one();
    }
}

int main(int argc, char const *argv[])
{
    struct device *dev;
    if (argc < 2) {
        printf("Usage: %s <dev-name>\n", argv[0]);
        return 1;
    }

    if (device_open(argv[1], DO_RDWR, &dev)) {
        printf("open failed\n");
        return 1;
    }

    size_t elements = 100000;
    completions.reserve(elements);

    auto bio = alloc_bio();
    bio_buffer = memory::alloc_page();
    bio_clock = static_cast<unsigned long *>(bio_buffer);
    WITH_LOCK(bio_mutex) {
        for (unsigned int i = 0; i < elements; i++) {
            bio->bio_cmd = BIO_WRITE;
            bio->bio_dev = dev;
            *bio_clock = clock::get()->time();
            bio->bio_data = bio_buffer;
            bio->bio_offset = 4 << 20;
            bio->bio_bcount = 4 * KB;
            bio->bio_caller1 = bio;
            bio->bio_done = bio_done;

            dev->driver->devops->strategy(bio);
            wait_bio.wait(&bio_mutex);
        }
    }
    memory::free_page(bio_buffer);

    auto size = completions.size();
    std::sort(completions.begin(), completions.end());
    int msec = 1000000;

    std::cout << "Min      50%      90%      99%      99.99%   99.999%  Max     [msec]\n";
    std::cout << "---      ---      ---      ---      ------   -------  ---\n";
    printf("%-8.4f ", float(completions[0]) / msec);
    printf("%-8.4f ", float(completions[size / 2]) / msec );
    printf("%-8.4f ", float(completions[(9 * size) / 100]) / msec);
    printf("%-8.4f ", float(completions[(99 * size) / 100]) / msec);
    printf("%-8.4f ", float(completions[(999 * size) / 1000])/ msec);
    printf("%-8.4f ", float(completions[(9999 * size) / 10000])/ msec);
    printf("%-8.4f ", float(completions.back()) / msec);
    printf("\n");

    return 0;
}
