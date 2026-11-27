/* Copyright (C) 2026 Greg Burd. */
// Guest boot control; optional real block-device read, not a durability test.
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
int main(int argc, char** argv) {
    if (argc > 1) {
        int fd = open(argv[1], O_RDONLY);
        unsigned char block[512];
        if (fd < 0 || read(fd, block, sizeof(block)) != sizeof(block)) {
            perror("Crucible guest device read"); return 1;
        }
        close(fd);
        for (auto byte : block) { if (byte) return 2; }
        puts("PASS Crucible guest real device zero read");
    }
    puts("PASS Crucible-enabled OSv guest application");
    return 0;
}
