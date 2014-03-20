/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "macros.S"

/* XXX this will halt, because it is not reached/tested.
   When we reach this, test, verify, develop against the ELF AArch64 ABI */
.text

.global __elf_resolve_pltgot

// calling convention:
// sp + 8: index
// sp + 0: object
__elf_resolve_pltgot:
        .type __elf_resolve_pltgot, @function

1:      wfi
        b 1b
        sub sp, sp, #16            // make room for resolved address

        /* XXX
         * push caller-saved gp/fp-regs,
         * elf_resolve_pltgot passing x0 and x1 with the args,
         * restore caller-saved gp/fp-regs. */

        add sp, sp, #16
        ret
