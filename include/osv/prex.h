#ifndef _OSV_PREX_H
#define _OSV_PREX_H 1


#include <sys/cdefs.h>
#include <unistd.h>

__BEGIN_DECLS

#define TASK_NULL	  ((task_t)0)
#define	BSIZE	512		/* size of secondary block (bytes) */

/*
 * Kernel encoding of open mode; separate read and write bits that are
 * independently testable: 1 greater than the above.
 */
#define FREAD           0x00000001
#define FWRITE          0x00000002

/* convert from open() flags to/from fflags; convert O_RD/WR to FREAD/FWRITE */
#define FFLAGS(oflags)  ((oflags) + 1)
#define OFLAGS(fflags)  ((fflags) - 1)

#define PAGE_SIZE	4096
#define PAGE_MASK	(PAGE_SIZE-1)
#define round_page(x)	(((x) + PAGE_MASK) & ~PAGE_MASK)


typedef unsigned long   object_t;
typedef unsigned long   task_t;
typedef unsigned long   thread_t;
typedef unsigned long   mutex_t;
typedef unsigned long   cond_t;
typedef unsigned long   sem_t;
typedef unsigned long   device_t;

size_t strlcat(char *dst, const char *src, size_t siz);
size_t strlcpy(char *dst, const char *src, size_t siz);

void sys_panic(const char *);

__END_DECLS

#endif /* _OSV_PREX_H */
