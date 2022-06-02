#include <dirent.h>     /* Defines DT_* constants */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <cassert>

#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

struct test_dirent64 {
    unsigned long  d_ino;
    off_t          d_off;
    unsigned char  d_type;
    std::string    d_name;

    bool operator ==(const test_dirent64 &b) const {
       return d_ino == b.d_ino &&
              d_off == b.d_off &&
              d_type == b.d_type &&
              d_name == b.d_name;
    }
};

// This code is loosely based on the example found under https://man7.org/linux/man-pages/man2/getdents.2.html
void test_getdents64(const char *dir_path, size_t buf_size, std::vector<test_dirent64> &dirents) {
    struct linux_dirent64 {
        unsigned long  d_ino;
        off_t          d_off;
        unsigned short d_reclen;
        unsigned char  d_type;
        char           d_name[];
    };

    int fd = open(dir_path, O_RDONLY | O_DIRECTORY);
    if (fd == -1)
        handle_error("open");

    std::unique_ptr<char []> buf_ptr(new char[buf_size]);
    char *buf = buf_ptr.get();

    for (;;) {
        long nread = syscall(SYS_getdents64, fd, buf, buf_size);
        if (nread == -1)
            handle_error("getdents64");

        if (nread == 0)
            break;

        printf("--------------- nread=%ld ---------------\n", nread);
        printf("inode#    file type  d_reclen  d_off   d_name\n");
        for (long bpos = 0; bpos < nread;) {
            auto *d = (struct linux_dirent64 *) (buf + bpos);
            printf("%8ld  ", d->d_ino);

            char d_type = d->d_type;
            printf("%-10s ", (d_type == DT_REG) ?  "regular" :
                             (d_type == DT_DIR) ?  "directory" :
                             (d_type == DT_FIFO) ? "FIFO" :
                             (d_type == DT_SOCK) ? "socket" :
                             (d_type == DT_LNK) ?  "symlink" :
                             (d_type == DT_BLK) ?  "block dev" :
                             (d_type == DT_CHR) ?  "char dev" : "???");

            printf("%4d %10jd   %s\n", d->d_reclen,
                    (intmax_t) d->d_off, d->d_name);
            bpos += d->d_reclen;

            test_dirent64 dirent;
            dirent.d_ino = d->d_ino;
            dirent.d_off = d->d_off;
            dirent.d_type = d_type;
            dirent.d_name = d->d_name;
            dirents.push_back(dirent);
        }
    }

    close(fd);
}

#define LARGE_BUF_SIZE 1024
#define SMALL_BUF_SIZE 128

int main()
{
    // Verify that getdents64 works correctly against /proc directory and yields
    // correct results
    std::vector<test_dirent64> dirents_1;
    test_getdents64("/proc", LARGE_BUF_SIZE, dirents_1);

    assert(std::count_if(dirents_1.begin(), dirents_1.end(), [](test_dirent64 d) { return d.d_type == DT_REG; }) >= 3);
    assert(std::count_if(dirents_1.begin(), dirents_1.end(), [](test_dirent64 d) { return d.d_type == DT_DIR; }) >= 5);

    assert(std::find_if(dirents_1.begin(), dirents_1.end(), [](test_dirent64 d) { return d.d_name == ".."; }) != dirents_1.end());
    assert(std::find_if(dirents_1.begin(), dirents_1.end(), [](test_dirent64 d) { return d.d_name == "cpuinfo"; }) != dirents_1.end());
    assert(std::find_if(dirents_1.begin(), dirents_1.end(), [](test_dirent64 d) { return d.d_name == "sys"; }) != dirents_1.end());

    // Verify that getdents64 works with smaller buffer and yields same results as above
    std::vector<test_dirent64> dirents_2;
    test_getdents64("/proc", SMALL_BUF_SIZE, dirents_2);

    assert(std::equal(dirents_1.begin(), dirents_1.end(), dirents_2.begin()));
}
