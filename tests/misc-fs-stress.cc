#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <atomic>

#include "stat.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static std::chrono::high_resolution_clock s_clock;

#define MB (1024*1024)

float perc(float p, std::vector<float> samples)
{
    return samples[p * (samples.size() - 1)];
}

static void print_distribution(std::vector<float> samples)
{
    std::sort(samples.begin(), samples.end());
    printf("Latency of write() [s]:\n");
    printf("0     %.9f\n", perc(0, samples));
    printf("0.5   %.9f\n", perc(0.5, samples));
    printf("0.9   %.9f\n", perc(0.9, samples));
    printf("0.99  %.9f\n", perc(0.99, samples));
    printf("0.999 %.9f\n", perc(0.999, samples));
    printf("1.0   %.9f\n", perc(1, samples));
}

int main(int argc, char const *argv[])
{
    const int buf_size = 1024;
    char *buf = new char[buf_size];
    const char * fname;
    char default_fname[64] = "/tmpfileXXXXXX";

    const std::chrono::seconds test_duration(10);

    if (argc > 2) {
        printf("Usage: %s <file-name>\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        fname = argv[1];
    } else {
        mktemp(default_fname);
        fname = reinterpret_cast<const char *>(default_fname);
    }

    int fd = open(fname, O_CREAT | O_RDWR | O_LARGEFILE | O_DIRECT);
    FILE *f = fdopen(fd, "w");

    std::atomic<long> stat_bytes_written(0);
    long total = 0;

    auto test_start = s_clock.now();
    auto end_at = test_start + test_duration;
    std::vector<float> samples;

    stat_printer _stat_printer(stat_bytes_written, [] (float bytes_per_second) {
        printf("%.3f Mb/s\n", (float)bytes_per_second / MB);
    }, 1000);

    while (s_clock.now() < end_at) {
        auto start = s_clock.now();
        if (fwrite(buf, buf_size, 1, f) != 1) {
            perror("write");
            return 1;
        }
        auto end = s_clock.now();
        samples.push_back(to_seconds(end - start));

        stat_bytes_written += buf_size;
        total += buf_size;
    }

    auto test_end = s_clock.now();
    _stat_printer.stop();

    auto actual_test_duration = to_seconds(test_end - test_start);
    printf("Wrote %.3f MB in %.2f s = %.3f Mb/s\n", (double) total / MB, actual_test_duration,
        (double) total / MB / actual_test_duration);

    printf("\n");
    print_distribution(samples);

    fclose(f);
    remove(fname);
    return 0;
}
