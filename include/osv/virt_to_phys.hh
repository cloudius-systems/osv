/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRT_TO_PHYS_HH
#define VIRT_TO_PHYS_HH

namespace mmu {

typedef uint64_t phys;
phys virt_to_phys(void *virt);

};


#endif /* VIRT_TO_PHYS_HH */

