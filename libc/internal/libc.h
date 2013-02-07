#ifndef LIBC_H
#define LIBC_H

#include <stdlib.h>
#include "stdio.h"

/* as long as we use the glibc header we'll need this hack */
#ifndef O_LARGEFILE
#define O_LARGEFILE	0
#endif

struct __libc {
	void *main_thread;
	int threaded;
//	int secure;
//	size_t *auxv;
//	int (*atexit)(void (*)(void));
//	void (*fini)(void);
//	void (*ldso_fini)(void);
	volatile int threads_minus_1;
//	int canceldisable;
	FILE *ofl_head;
	int ofl_lock[2];
//	size_t tls_size;
};

#define ATTR_LIBC_VISIBILITY __attribute__((visibility("hidden")))

extern struct __libc __libc ATTR_LIBC_VISIBILITY;
#define libc __libc

/* Designed to avoid any overhead in non-threaded processes */
void __lock(volatile int *) ATTR_LIBC_VISIBILITY;
void __unlock(volatile int *) ATTR_LIBC_VISIBILITY;
int __lockfile(FILE *) ATTR_LIBC_VISIBILITY;
void __unlockfile(FILE *) ATTR_LIBC_VISIBILITY;
#define LOCK(x) (libc.threads_minus_1 ? (__lock(x),1) : ((void)(x),1))
#define UNLOCK(x) (libc.threads_minus_1 ? (__unlock(x),1) : ((void)(x),1))

extern char **__environ;
#define environ __environ

#undef weak_alias
#define weak_alias(old, new) \
	extern __typeof(old) new __attribute__((weak, alias(#old)))

#undef LFS64_2
#define LFS64_2(x, y) weak_alias(x, y)

#undef LFS64
#define LFS64(x) LFS64_2(x, x##64)

#endif
