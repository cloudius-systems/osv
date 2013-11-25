#include <sys/mman.h>
#include <cstdio>
#include <chrono>

std::chrono::duration<double> mmap_and_write(size_t mb, int flags)
{
    size_t size = mb*1024*1024;
    char *p = reinterpret_cast<char*>(mmap(nullptr, size, PROT_READ|PROT_WRITE, flags|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0));
    auto start = std::chrono::system_clock::now();
    for (size_t i = 0; i < size; i += 4096) {
        p[i] = 0xfe;
    }
    auto end = std::chrono::system_clock::now();
    munmap(p, size);
    return end - start;
}

void mmap_bench(size_t mb)
{
    auto demand   = mmap_and_write(mb, 0);
    auto populate = mmap_and_write(mb, MAP_POPULATE);

    printf("%4lu %-6.3f %-6.3f\n", mb, demand.count(), populate.count());
}

int main()
{
    for (auto i = 1; i <= 5; i++) {
        printf("Iteration %d\n\n", i);
        printf("     time (seconds)\n");
        printf(" MiB demand populate\n");

        for (auto mb = 1; mb <= 1024; mb *= 2) {
            mmap_bench(mb);
        }

        printf("\n");
    }
}
