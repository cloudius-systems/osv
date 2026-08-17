// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL kstat - standalone header providing the kstat interface
 * that OpenZFS expects from the platform SPL layer.
 */
#ifndef _SPL_OSV_KSTAT_H
#define	_SPL_OSV_KSTAT_H

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* kstat types */
#define	KSTAT_TYPE_RAW		0
#define	KSTAT_TYPE_NAMED	1
#define	KSTAT_TYPE_INTR		2
#define	KSTAT_TYPE_IO		3
#define	KSTAT_TYPE_TIMER	4

/* kstat flags */
#define	KSTAT_FLAG_VIRTUAL	0x01
#define	KSTAT_FLAG_WRITABLE	0x02
#define	KSTAT_FLAG_PERSISTENT	0x04
#define	KSTAT_FLAG_DORMANT	0x08
#define	KSTAT_FLAG_INVALID	0x10
#define	KSTAT_FLAG_LONGSTRINGS	0x20
#define	KSTAT_FLAG_NO_HEADERS	0x40

/* kstat read/write for update callback */
#define	KSTAT_READ		0
#define	KSTAT_WRITE		1

struct kstat;
typedef int kstat_update_t(struct kstat *, int);

typedef struct kstat {
	void		*ks_data;
	u_int		 ks_ndata;
	size_t		 ks_data_size;
	uchar_t		 ks_flags;
	kstat_update_t	*ks_update;
	void		*ks_private;
	void		*ks_private1;
	void		*ks_lock;
} kstat_t;

/* kstat data types */
#define	KSTAT_DATA_CHAR		0
#define	KSTAT_DATA_INT32	1
#define	KSTAT_DATA_UINT32	2
#define	KSTAT_DATA_INT64	3
#define	KSTAT_DATA_UINT64	4
#define	KSTAT_DATA_LONG		5
#define	KSTAT_DATA_ULONG	6
#define	KSTAT_DATA_STRING	7

typedef struct kstat_named {
#define	KSTAT_STRLEN	31
	char	name[KSTAT_STRLEN + 1];
	uchar_t	data_type;
	union {
		char		c[16];
		int32_t		i32;
		uint32_t	ui32;
		int64_t		i64;
		uint64_t	ui64;
		long		l;
		unsigned long	ul;
		struct {
			union {
				char	*ptr;
				char	__pad[8];
			} addr;
			uint32_t	len;
		} string;
	} value;
} kstat_named_t;

#define	KSTAT_NAMED_STR_PTR(knp)	((knp)->value.string.addr.ptr)
#define	KSTAT_NAMED_STR_BUFLEN(knp)	((knp)->value.string.len)

typedef struct kstat_intr {
	uint_t	intrs[5];
} kstat_intr_t;

typedef struct kstat_io {
	u_longlong_t	nread;
	u_longlong_t	nwritten;
	uint_t		reads;
	uint_t		writes;
	hrtime_t	wtime;
	hrtime_t	wlentime;
	hrtime_t	wlastupdate;
	hrtime_t	rtime;
	hrtime_t	rlentime;
	hrtime_t	rlastupdate;
	uint_t		wcnt;
	uint_t		rcnt;
} kstat_io_t;

kstat_t *kstat_create(const char *module, int instance, const char *name,
    const char *cls, uchar_t type, ulong_t ndata, uchar_t flags);
void kstat_install(kstat_t *ksp);
void kstat_delete(kstat_t *ksp);
void kstat_waitq_enter(kstat_io_t *kiop);
void kstat_waitq_exit(kstat_io_t *kiop);
void kstat_runq_enter(kstat_io_t *kiop);
void kstat_runq_exit(kstat_io_t *kiop);

typedef int (*kstat_raw_reader_t)(char *buf, size_t size, void *data);

static inline void
kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t n))
{
	(void) ksp; (void) headers; (void) data; (void) addr;
}

static inline void
kstat_named_init(kstat_named_t *knp, const char *name, uchar_t type)
{
	strncpy(knp->name, name, KSTAT_STRLEN);
	knp->name[KSTAT_STRLEN] = '\0';
	knp->data_type = type;
}

static inline void
kstat_set_string(char *dst, const char *src)
{
	strncpy(dst, src, KSTAT_STRLEN);
	dst[KSTAT_STRLEN] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_KSTAT_H */
