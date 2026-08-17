// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv module parameter support.
 *
 * OSv is a unikernel - there are no loadable modules or sysctl.
 * ZFS_MODULE_PARAM just declares the variable (which is defined elsewhere).
 *
 * We provide stub sysctl types so that FreeBSD-style parameter callback
 * functions (e.g., spa_taskq_read_param) compile without modification.
 */
#ifndef _SPL_OSV_MOD_OS_H
#define	_SPL_OSV_MOD_OS_H

/*
 * Stub sysctl types for FreeBSD-compatible parameter callbacks.
 * Some OpenZFS code (spa.c) uses SYSCTL_HANDLER_ARGS-style functions
 * in the non-__linux__ code path.
 */
struct sysctl_oid;
struct sysctl_req {
	void	*newptr;
	void	*oldptr;
};

static inline int
sysctl_handle_string(struct sysctl_oid *oidp, void *arg1, size_t arg2,
    struct sysctl_req *req)
{
	(void) oidp;
	(void) arg1;
	(void) arg2;
	(void) req;
	return (0);
}

static inline int
sysctl_handle_64(struct sysctl_oid *oidp, void *arg1, intmax_t arg2,
    struct sysctl_req *req)
{
	(void) oidp;
	(void) arg1;
	(void) arg2;
	(void) req;
	return (0);
}

static inline int
sysctl_handle_int(struct sysctl_oid *oidp, void *arg1, intmax_t arg2,
    struct sysctl_req *req)
{
	(void) oidp;
	(void) arg1;
	(void) arg2;
	(void) req;
	return (0);
}

/* Match FreeBSD SYSCTL_HANDLER_ARGS style */
#define	ZFS_MODULE_PARAM_ARGS \
	struct sysctl_oid *oidp, void *arg1, intmax_t arg2, \
	struct sysctl_req *req

/* No sysctl on OSv - parameter macros are no-ops */
#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc)
#define	ZFS_MODULE_PARAM_CALL(scope, prefix, name, func, type, perm, desc)
#define	ZFS_MODULE_VIRTUAL_PARAM_CALL	ZFS_MODULE_PARAM_CALL

/* Module init/exit are called explicitly on OSv */
#define	module_init(fn)
#define	module_init_early(fn)
#define	module_exit(fn)

/* Exported symbols - no-op on OSv (statically linked) */
#define	EXPORT_SYMBOL(x)

#endif /* _SPL_OSV_MOD_OS_H */
