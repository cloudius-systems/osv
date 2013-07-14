#ifndef PAGEALLOC_HH_
#define PAGEALLOC_HH_

#include <stddef.h>

namespace memory {

void* alloc_page();
void free_page(void* page);
void* alloc_huge_page(size_t bytes);
void free_huge_page(void *page, size_t bytes);

}

#endif /* PAGEALLOC_HH_ */
