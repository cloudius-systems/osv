// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_KMEM_CACHE_H
#define	_SPL_OSV_KMEM_CACHE_H

#include <sys/kmem.h>

/* kmem move callback return values (OpenZFS expects these) */
#ifndef KMEM_CBRC_YES
typedef enum kmem_cbrc {
	KMEM_CBRC_YES		= 0,
	KMEM_CBRC_NO		= 1,
	KMEM_CBRC_LATER		= 2,
	KMEM_CBRC_DONT_NEED	= 3,
	KMEM_CBRC_DONT_KNOW	= 4,
} kmem_cbrc_t;
#endif

/* kmem_cache_set_move already defined in compat kmem.h */

#endif
