// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL sunddi - standalone (avoids compat chain through kmem/sysevent)
 */
#ifndef _SPL_OSV_SUNDDI_H
#define	_SPL_OSV_SUNDDI_H

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/u8_textprep.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal sysevent_id_t definition.
 * The full sysevent.h pulls in nvpair.h which is fine, but we need
 * the type available early. Use hrtime_t = long long.
 */
typedef struct sysevent_id {
	uint64_t	eid_seq;
	long long	eid_ts;
} sysevent_id_t;

/* Forward declare nvlist_t */
struct nvlist;
typedef struct nvlist nvlist_t;

#define	ddi_driver_major(zfs_dip)		(0)

/* copyin/copyout - OSv is single address space */
extern int copyin(const void *, void *, size_t);
extern int copyout(const void *, void *, size_t);

#define	ddi_copyin(from, to, size, flag)		\
	(copyin((from), (to), (size)), 0)
#define	ddi_copyout(from, to, size, flag)		\
	(copyout((from), (to), (size)), 0)

int ddi_strtol(const char *str, char **nptr, int base, long *result);
int ddi_strtoul(const char *str, char **nptr, int base, unsigned long *result);
int ddi_strtoull(const char *str, char **nptr, int base,
    unsigned long long *result);

#define	DDI_SUCCESS	(0)
#define	DDI_FAILURE	(-1)
#define	DDI_SLEEP	0x666

int ddi_soft_state_init(void **statep, size_t size, size_t nitems);
void ddi_soft_state_fini(void **statep);
void *ddi_get_soft_state(void *state, int item);
int ddi_soft_state_zalloc(void *state, int item);
void ddi_soft_state_free(void *state, int item);

int _ddi_log_sysevent(char *vendor, char *class_name, char *subclass_name,
    nvlist_t *attr_list, sysevent_id_t *eidp, int flag);
#define	ddi_log_sysevent(dip, vendor, class_name, subclass_name,	\
	    attr_list, eidp, flag)					\
	_ddi_log_sysevent((vendor), (class_name), (subclass_name),	\
	    (attr_list), (eidp), (flag))

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_SUNDDI_H */
