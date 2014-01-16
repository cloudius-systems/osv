#ifndef _STDIO_IMPL_H
#define _STDIO_IMPL_H

#include "stdio.h"
#include "../internal/libc.h"
#include <pthread.h>

__BEGIN_DECLS

#define UNGET 8

#define FFINALLOCK(f) \
	((f)->no_locking ? 0 : __lockfile((f)))
#define FLOCK(f) \
	int __need_unlock = ((f)->no_locking ? 0 : __lockfile((f)))
#define FUNLOCK(f) if (__need_unlock) __unlockfile((f)); else

#define F_PERM 1
#define F_NORD 4
#define F_NOWR 8
#define F_EOF 16
#define F_ERR 32
#define F_SVB 64

/*
 * Note: this structure is layed out so that the fields which are accessed
 * by the unlocked getc/putc macros is identical to glibc.  Make sure to
 * consult glibc before changing the layout of any fields.  Adding new fields
 * should be generally okay.
 *
 * See musl commit e3cd6c5c265cd481db6e0c5b529855d99f0bda30 for some more
 * details.
 */
struct __FILE_s {
	unsigned flags;
	unsigned char *rpos, *rend;
	int (*close)(FILE *);
	unsigned char *wend, *wpos;
	unsigned char *mustbezero_1;
	unsigned char *wbase;
	size_t (*read)(FILE *, unsigned char *, size_t);
	size_t (*write)(FILE *, const unsigned char *, size_t);
	off_t (*seek)(FILE *, off_t, int);
	unsigned char *buf;
	size_t buf_size;
	FILE *prev, *next;
	int fd;
	int pipe_pid;
	long lockcount;
	short dummy3;
	signed char mode;
	signed char lbf;
	int dummy4;
	int waiters;
	void *cookie;
	off_t off;
	char *getln_buf;
	void *mustbezero_2;
	unsigned char *shend;
	off_t shlim, shcnt;

	bool no_locking;
	mutex_t mutex;
};

size_t __stdio_read(FILE *, unsigned char *, size_t);
size_t __stdio_write(FILE *, const unsigned char *, size_t);
size_t __stdout_write(FILE *, const unsigned char *, size_t);
off_t __stdio_seek(FILE *, off_t, int);
int __stdio_close(FILE *);

size_t __string_read(FILE *, unsigned char *, size_t);

int __toread(FILE *);
int __towrite(FILE *);

#if defined(__PIC__) && (100*__GNUC__+__GNUC_MINOR__ >= 303)
__attribute__((visibility("protected")))
#endif
int __overflow(FILE *, int), __uflow(FILE *);

int __fseeko(FILE *, off_t, int);
int __fseeko_unlocked(FILE *, off_t, int);
off_t __ftello(FILE *);
off_t __ftello_unlocked(FILE *);
size_t __fwritex(const unsigned char *, size_t, FILE *);
int __putc_unlocked(int, FILE *);

FILE *__fdopen(int, const char *);
int __fmodeflags(const char *);

#define OFLLOCK() LOCK(libc.ofl_lock)
#define OFLUNLOCK() UNLOCK(libc.ofl_lock)

#define feof(f) ((f)->flags & F_EOF)
#define ferror(f) ((f)->flags & F_ERR)

#define getc_unlocked(f) \
	( ((f)->rpos < (f)->rend) ? *(f)->rpos++ : __uflow((f)) )

#define putc_unlocked(c, f) ( ((c)!=(f)->lbf && (f)->wpos<(f)->wend) \
	? *(f)->wpos++ = (c) : __overflow((f),(c)) )

/* Caller-allocated FILE * operations */
FILE *__fopen_rb_ca(const char *, FILE *, unsigned char *, size_t);
int __fclose_ca(FILE *);

int __lockfile(FILE *) ATTR_LIBC_VISIBILITY;
void __unlockfile(FILE *) ATTR_LIBC_VISIBILITY;

__END_DECLS

#endif
