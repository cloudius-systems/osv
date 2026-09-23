#ifndef STDLIB_H
#define STDLIB_H

#include "../stdlib.h"

hidden int __putenv(char *, size_t, char *);
hidden void __env_rm_add(char *, char *);
hidden int __mkostemps(char *, int, int);
hidden int __ptsname_r(int, char *, size_t);
hidden char *__randname(char *);
/* musl 1.2.3 moved qsort() into qsort_nr.c, which calls this. */
hidden void __qsort_r (void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);

#endif

