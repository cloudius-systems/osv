/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <chrono>

#define BUF_SIZE        4096

static double to_msec(double sec)
{
    return sec * 1000;
}

static const char *files[] = {
    "/libuutil.so",
    "/zpool.so",
    "/libzfs.so",
    "/zfs.so",
    "/tools/mkfs.so",
    "/tools/cpiod.so"
};

static double total_time;
static unsigned long total_files;
static unsigned long total_read_bytes;

static void read_file(const char *path)
{
    struct stat st;
    char buf[BUF_SIZE];
    int fd;
    ssize_t read_bytes;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    auto begin = std::chrono::high_resolution_clock::now();
    while ((read_bytes = read(fd, buf, sizeof(buf))) > 0)
        ;
    auto end = std::chrono::high_resolution_clock::now();

    assert(read_bytes == 0); // Assert EOF at this point.
    assert(stat(path, &st) == 0);
    total_read_bytes += st.st_size;
    total_files++;

    std::chrono::duration<double> sec = end - begin;
    total_time += sec.count();
    printf("%s: %d%s: %.2fms, (+%.2fms)\n", path,
        (st.st_size < 1024) ? st.st_size : st.st_size / 1024,
        (st.st_size < 1024) ? "b" : "kb",
        to_msec(total_time), to_msec(sec.count()));

    close(fd);
}

static void path_cat(char *path, const char *dir, const char *d_name)
{
    int length = snprintf(path, PATH_MAX, "%s/%s", dir, d_name);
    if (length >= PATH_MAX) {
        fprintf(stderr, "Path too long.\n");
        exit(EXIT_FAILURE);
    }
}

static void list_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        struct dirent *entry;
        const char *d_name;
        char path[PATH_MAX];

        entry = readdir(d);
        if (!entry) {
            break;
        }
        d_name = entry->d_name;

        if (entry->d_type & DT_DIR) {
            if (strcmp(d_name, "..") != 0 && strcmp(d_name, ".") != 0) {
                path_cat(path, dir, d_name);
                list_dir(path);
            }
        } else if (entry->d_type & DT_REG) {
            path_cat(path, dir, d_name);
            read_file(path);
        }
    }

    if (closedir(d)) {
        perror("closedir");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    int filesc = sizeof(files) / sizeof(files[0]);

    // Read some arbitrary shared object files
    for (int i = 1; i < filesc; i++) {
        read_file(files[i]);
    }

    // Read all JVM files
    list_dir("/usr/lib/jvm");

    printf("\nREPORT\n-----\n"
           "Files:\t%d\n"
           "Read:\t%dkb\n"
           "Time:\t%.2fms\n"
           "MBps:\t%.2f\n",
           total_files, total_read_bytes / 1024UL, to_msec(total_time),
           (total_read_bytes >> 20) / total_time);

    return 0;
}
