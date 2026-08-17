/*
 * OSv stub for sys/mod.h - ZFS module parameter registration.
 * OSv has no sysctl/tunable framework, so these are all no-ops.
 *
 * We model ZFS_MODULE_PARAM_ARGS after FreeBSD's SYSCTL_HANDLER_ARGS so
 * that the FreeBSD code path in OpenZFS compiles (the functions are dead
 * code since ZFS_MODULE_PARAM_CALL is a no-op, but they must parse cleanly).
 */
#ifndef _OSV_SPL_MOD_H
#define	_OSV_SPL_MOD_H

/* Minimal sysctl stub types needed by the FreeBSD code path. */
struct osv_sysctl_oid;
struct osv_sysctl_req {
	void	*newptr;
};
static inline int
sysctl_handle_string(struct osv_sysctl_oid *oidp __attribute__((unused)),
    char *buf __attribute__((unused)),
    size_t len __attribute__((unused)),
    struct osv_sysctl_req *req __attribute__((unused)))
{
	return (0);
}
static inline int
sysctl_handle_64(struct osv_sysctl_oid *oidp __attribute__((unused)),
    void *arg __attribute__((unused)),
    int arg2 __attribute__((unused)),
    struct osv_sysctl_req *req __attribute__((unused)))
{
	return (0);
}
static inline int
sysctl_handle_int(struct osv_sysctl_oid *oidp __attribute__((unused)),
    void *arg __attribute__((unused)),
    int arg2 __attribute__((unused)),
    struct osv_sysctl_req *req __attribute__((unused)))
{
	return (0);
}

/* ZFS_MODULE_PARAM_ARGS - provides oidp, arg1, arg2, req to FreeBSD-path fns */
#define	ZFS_MODULE_PARAM_ARGS	\
	struct osv_sysctl_oid *oidp __attribute__((unused)),		\
	void *arg1 __attribute__((unused)),				\
	intptr_t arg2 __attribute__((unused)),				\
	struct osv_sysctl_req *req __attribute__((unused))

#define	ZMOD_RW	0
#define	ZMOD_RD	1

#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc)
#define	ZFS_MODULE_PARAM_CALL(scope_prefix, name_prefix, name, setfunc, \
	getfunc, perm, desc)
#define	ZFS_MODULE_VIRTUAL_PARAM_CALL ZFS_MODULE_PARAM_CALL

#define	EXPORT_SYMBOL(x)
#define	module_init(fn)
#define	module_init_early(fn)
#define	module_exit(fn)

#endif /* _OSV_SPL_MOD_H */
