#include "pthread_impl.h"
#include <sys/mman.h>

int pthread_getattr_np(pthread_t t, pthread_attr_t *a)
{
	*a = (pthread_attr_t){0};
	a->_a_detach = !!t->detached;
	if (t->stack) {
		a->_a_stackaddr = (uintptr_t)t->stack;
		a->_a_stacksize = t->stack_size - DEFAULT_STACK_SIZE;
	} else {
		char *p = (void *)libc.auxv;
		size_t l = PAGE_SIZE;
		p += -(uintptr_t)p & PAGE_SIZE-1;
		a->_a_stackaddr = (uintptr_t)p;
		while (!posix_madvise(p-l-PAGE_SIZE, PAGE_SIZE, POSIX_MADV_NORMAL))
			l += PAGE_SIZE;
		a->_a_stacksize = l - DEFAULT_STACK_SIZE;
	}
	return 0;
}
