/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * musl 1.2.x's __randname.c mixes the thread id into the seed by
 * reaching into musl's struct pthread via pthread_impl.h, which OSv
 * does not use.  This keeps the same seed recipe and gets the tid from
 * OSv's gettid() instead.  Same output distribution, same six
 * characters, no dependency on musl's thread internals.
 */

#include <time.h>
#include <stdint.h>

extern long gettid(void);
extern int __clock_gettime(clockid_t, struct timespec *);

/* This assumes that a check for the
   template size has already been made */
char *__randname(char *template)
{
	int i;
	struct timespec ts;
	unsigned long r;

	__clock_gettime(CLOCK_REALTIME, &ts);
	r = ts.tv_sec + ts.tv_nsec + (unsigned long)gettid() * 65537UL;
	for (i=0; i<6; i++, r>>=5)
		template[i] = 'A'+(r&15)+(r&16)*2;

	return template;
}
