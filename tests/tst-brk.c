#include <unistd.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <syscall.h>

#define PAGE_SIZE 4096
#define HUGE_PAGE_SIZE (512 * PAGE_SIZE)

static long free_memory()
{
    struct sysinfo info;
    if (sysinfo(&info) == -1)
        exit(-1);

    return info.freeram;
}

int main()
{
    // NULL
    assert(!brk(NULL));

    // Attempt to set program break that cannot be satisfied
    struct sysinfo info;
    if (sysinfo(&info) == -1)
        exit(-1);

    void *current_break = sbrk(0);
    assert(current_break);
    assert(brk(current_break + 2 * info.totalram));
    assert(errno == ENOMEM);

    // Same with a syscall
    assert(syscall(__NR_brk, current_break + 2 * info.totalram) == (long)current_break);
    assert(errno == ENOMEM);

    // Advance program break by a page
    long new_area_size = PAGE_SIZE;
    assert(!brk(current_break + new_area_size));
    assert(sbrk(0) == current_break + new_area_size);
    assert(free_memory() <= info.freeram - HUGE_PAGE_SIZE);
    //
    // Same with a syscall
    current_break = sbrk(0);
    assert(syscall(__NR_brk, current_break + new_area_size) == (long)(current_break + new_area_size));
    assert(sbrk(0) == current_break + new_area_size);

    // Advance program break back and forth by a huge page
    // and see if physical memory will be deallocated
    current_break = sbrk(0);
    long free_ram = free_memory();
    new_area_size = HUGE_PAGE_SIZE;
    assert(!brk(current_break + new_area_size));
    assert(sbrk(0) == current_break + new_area_size);
    assert(free_memory() <= free_ram - new_area_size);
    current_break = sbrk(0);
    assert(!brk(current_break - new_area_size));
    assert(free_memory() >= free_ram);
    //
    // Advance program break back and forth by 3 huge pages
    // and using sbrk and see if physical memory will be deallocated
    current_break = sbrk(0);
    free_ram = free_memory();
    new_area_size = 3 * HUGE_PAGE_SIZE;
    assert(sbrk(new_area_size) == current_break);
    assert(current_break + new_area_size == sbrk(0));
    assert(free_memory() <= free_ram - new_area_size);
    assert(sbrk(-new_area_size) == current_break + new_area_size);
    assert(current_break == sbrk(0));
    assert(free_memory() >= free_ram);
}
