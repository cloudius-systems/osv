#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static int write_pattern(int fd, size_t size, unsigned char pattern, int mmap_flags, size_t offset)
{
    auto* p = reinterpret_cast<unsigned char*>(mmap(NULL, size, PROT_READ|PROT_WRITE, mmap_flags, fd, offset));
    if (p == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    memset(p, pattern, size);
    if (munmap(p, size) < 0) {
        perror("munmap");
        return -1;
    }
    return 0;
}

static int verify_pattern(int fd, size_t size, unsigned char pattern, int mmap_flags, size_t offset)
{
    auto* p = reinterpret_cast<unsigned char*>(mmap(NULL, size, PROT_READ|PROT_WRITE, mmap_flags, fd, offset));
    if (p == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    for (size_t i = 0; i < size; i++) {
        if (p[i] != pattern) {
            printf("pattern didn't match\n");
            return -1;
        }
    }
    if (munmap(p, size) < 0) {
        perror("munmap");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    auto fd = open("/tmp/mmap-file-test", O_CREAT|O_TRUNC|O_RDWR, 0666);
    report(fd > 0, "open");
    constexpr int size = 8192;
    report(ftruncate(fd, size) == 0, "ftruncate");
    report(write_pattern(fd, size, 0xfe, MAP_SHARED, 0) == 0, "write pattern to MAP_SHARED");
    report(verify_pattern(fd, size, 0xfe, MAP_PRIVATE, 0) == 0, "verify pattern was written to file");
    report(write_pattern(fd, size, 0x0f, MAP_PRIVATE, 0) == 0, "write pattern to MAP_PRIVATE");
    report(verify_pattern(fd, size, 0xfe, MAP_PRIVATE, 0) == 0, "verify pattern didn't change");
    report(write_pattern(fd, size/2, 0x0f, MAP_SHARED, size/2) == 0, "write pattern to partial MAP_SHARED");
    report(verify_pattern(fd, size/2, 0xfe, MAP_PRIVATE, 0) == 0, "verify pattern didn't change in unmapped part");
    report(verify_pattern(fd, size/2, 0x0f, MAP_PRIVATE, size/2) == 0, "verify pattern changed in mapped part");
    report(close(fd) == 0, "close");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
