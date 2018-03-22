#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

#if defined(READ_ONLY_FS)
#define SUBDIR "rofs"
#else
#define SUBDIR "tmp"
#endif

int tests = 0, fails = 0;

using namespace std;

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                  ", expected " << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    return true;
}

int main()
{
    srand (time(NULL));
    //
    // First read first file using mmap
    string file_1("/" SUBDIR "/mmap-file-test1");
    int fd1 = open(file_1.c_str(), O_RDONLY, 0666);
    expect(fd1 >= 0, true );

    struct stat sb;
    if (fstat (fd1, &sb) == -1) {
        perror ("fstat");
        return 1;
    }

    if (!S_ISREG (sb.st_mode)) {
        cerr << file_1 << " is not a file\n";
        return 1;
    }

    off_t length = sb.st_size;
    void *address = mmap(0, length, PROT_READ, MAP_PRIVATE, fd1, 0);
    if (address == MAP_FAILED) {
        return 1;
    }

    string file_2("/" SUBDIR "/mmap-file-test2");
    int fd2 = open(file_2.c_str(), O_RDONLY, 0666);
    expect(fd2 >= 0, true );

    vector<thread> readers;

    int thread_count = 10, reads_count = 100;
    atomic<long> identical_count(0);
    for(int i = 0; i < thread_count; i++) {
        readers.emplace_back(thread([i,reads_count,fd2,length,address,&identical_count]{
            unsigned char buffer[4096];
            for(int step = 0; step < reads_count; step++) {
                off_t offset = rand() % length;
                size_t amount = min(length - offset, 4096l);

                int ret = pread(fd2, buffer, amount, offset);
                expect(ret > 0, true);
                if( ret > 0 ) {
                    auto is_identical = memcmp(buffer, static_cast<unsigned char*>(address) + offset, amount) == 0 ? 1 : 0;
                    identical_count += is_identical;
                }
                else {
                    cout << "[" << i << "] FAILED to read " << amount << " bytes at " << offset << "\n";
                }
            }
        }));
    }

    for(auto &t : readers) {
        t.join();
    }

    cout << "Identical count " << identical_count.load() << endl;

    munmap(address,length);
    close(fd1);
    close(fd2);

    expect(identical_count.load(),(long)(thread_count * reads_count));
}
