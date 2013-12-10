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

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/prex.h>

#define MB (1024*1024)
#define KB (1024)

static std::chrono::high_resolution_clock s_clock;

static void bio_done(struct bio* bio)
{
    free(bio->bio_data);
    destroy_bio(bio);
}

template<typename T>
static float to_seconds(T duration)
{
    return std::chrono::duration<float>(duration).count();
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

    const std::chrono::seconds test_duration(10);
    const std::chrono::seconds stat_period(1);
    const int buf_size = 4*KB;

    int bytes_written = 0;
    int total = 0;
    int offset = 0;

    auto test_start = s_clock.now();
    auto last_stat_dump = test_start;
    auto end_at = test_start + test_duration;

    while (s_clock.now() < end_at) {
        auto bio = alloc_bio();
        bio->bio_cmd = BIO_WRITE;
        bio->bio_dev = dev;
        bio->bio_data = malloc(buf_size);
        bio->bio_offset = offset;
        bio->bio_bcount = buf_size;
        bio->bio_caller1 = bio;
        bio->bio_done = bio_done;
        dev->driver->devops->strategy(bio);
        offset += buf_size;

        bytes_written += buf_size;
        total += buf_size;

        auto _now = s_clock.now();
        if (_now > last_stat_dump + stat_period) {
            auto period = to_seconds(_now - last_stat_dump);

            printf("%.3f Mb/s\n", (float)bytes_written / MB / period);

            last_stat_dump = _now;
            bytes_written = 0;
        }
    }

    printf("Wrote %.3f MB in %.2f s\n", (float) total / MB, to_seconds(s_clock.now() - test_start));
    return 0;
}