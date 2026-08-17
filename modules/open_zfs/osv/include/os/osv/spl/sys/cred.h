// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL cred - standalone (avoids compat chain through netport.h)
 */
#ifndef _SPL_OSV_CRED_H
#define	_SPL_OSV_CRED_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Credential type.
 * OSv is a single-user unikernel, so credentials are mostly no-ops.
 */
struct ucred;
typedef struct ucred cred_t;
typedef struct ucred ucred_t;

/*
 * kcred is used when you need all privileges.
 */
#define	kcred		NULL
#define	CRED()		NULL

#define	crgetuid(cred)		((uid_t)0)
#define	crgetruid(cred)		((uid_t)0)
#define	crgetgid(cred)		((gid_t)0)
#define	crgetgroups(cred)	((gid_t *)NULL)
#define	crgetngroups(cred)	(0)
#define	crgetsid(cred, i)	(NULL)
#define	crgetzoneid(cred)	((zoneid_t)0)
#define	crgetzone(cred)		(NULL)
#define	crhold(cred)		((void)(cred))
#define	crfree(cred)		((void)(cred))

/* Linux-style UID/GID conversions (no-ops on OSv) */
#define	KUID_TO_SUID(x)	(x)
#define	KGID_TO_SGID(x)	(x)

static inline int
groupmember(gid_t gid, const cred_t *cr)
{
	(void) gid;
	(void) cr;
	return (1); /* only one user and group in OSv */
}

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_CRED_H */
