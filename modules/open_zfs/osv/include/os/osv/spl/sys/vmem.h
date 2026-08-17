// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_VMEM_H
#define	_SPL_OSV_VMEM_H

#include <sys/kmem.h>

/* vmem is just kmem on OSv */
#ifndef vmem_alloc
#define	vmem_alloc(size, flags)		kmem_alloc(size, flags)
#endif
#ifndef vmem_zalloc
#define	vmem_zalloc(size, flags)	kmem_zalloc(size, flags)
#endif
#ifndef vmem_free
#define	vmem_free(ptr, size)		kmem_free(ptr, size)
#endif

#endif
