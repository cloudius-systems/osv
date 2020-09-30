#ifndef SYS_MMAN_H
#define SYS_MMAN_H

#include "../../sys/mman.h"

hidden void __vm_wait(void);
hidden void __vm_lock(void);
hidden void __vm_unlock(void);

void *__mmap(void *, size_t, int, int, int, off_t);
int __munmap(void *, size_t);
hidden void *__mremap(void *, size_t, size_t, int, ...);
hidden int __madvise(void *, size_t, int);
hidden int __mprotect(void *, size_t, int);

hidden const unsigned char *__map_file(const char *, size_t *);

hidden char *__shm_mapname(const char *, char *);

#endif
