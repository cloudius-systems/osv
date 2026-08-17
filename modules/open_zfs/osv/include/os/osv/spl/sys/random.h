// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL random - standalone (avoids compat chain through netport.h)
 */
#ifndef _SPL_OSV_RANDOM_H
#define	_SPL_OSV_RANDOM_H

#ifdef __cplusplus
extern "C" {
#endif

/* read_random is provided by the OSv compat layer */
extern int read_random(void *, int);

/*
 * read_random() returns the number of bytes filled.  On OSv it uses
 * arc4random and always fills the full requested count.  Callers of
 * random_get_bytes() expect 0 on success, so discard the count and
 * return 0.
 */
#define	random_get_bytes(p, s) \
	((void)read_random((p), (int)(s)), 0)
#define	random_get_pseudo_bytes(p, s) \
	((void)read_random((p), (int)(s)), 0)

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_RANDOM_H */
