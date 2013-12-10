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

template<typename T>
static float to_seconds(T duration)
{
    return std::chrono::duration<float>(duration).count();
}

int main(int argc, char const *argv[])
{
    const int buf_size = 1024;
    char *buf = new char[buf_size];

    const std::chrono::seconds test_duration(10);
    const std::chrono::seconds stat_period(1);

    if (argc < 2) {
        printf("Usage: %s <file-name>\n", argv[0]);
        return 1;
    }

    const char * fname = argv[1];

    int fd = open(fname, O_CREAT | O_RDWR | O_LARGEFILE | O_DIRECT);
    FILE *f = fdopen(fd, "w");

    int bytes_written = 0;
    int total = 0;

    auto test_start = s_clock.now();
    auto last_stat_dump = test_start;
    auto end_at = test_start + test_duration;
    std::vector<float> samples;

    while (s_clock.now() < end_at) {
        auto start = s_clock.now();
        if (fwrite(buf, buf_size, 1, f) != 1) {
            perror("write");
            return 1;
        }

        auto end = s_clock.now();
        samples.push_back(std::chrono::duration<float>(end - start).count());

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
    printf("\n");
    print_distribution(samples);

    fclose(f);
    remove(fname);
    return 0;
}
