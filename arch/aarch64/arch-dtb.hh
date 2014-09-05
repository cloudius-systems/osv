/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_DTB_HH
#define ARCH_DTB_HH

/* dtb is set early during boot */
extern void *dtb;

/* this information is also stored and exported in this module,
 * since it has to be collected at dtb setup time.
 * Only the boot loader references this, should never be used
 * elsewhere, as the pointed memory is overwritten with dtb.
 */
extern char *cmdline;

/* void __attribute__((constructor(init_prio::dtb))) dtb_setup()
 *
 * this is a constructor that is run at premain time with priority dtb,
 * in order to check the dtb contents for correctness, and on failure
 * avoid any fdt use by setting the global dtb pointer to NULL.
 * If dtb is valid, it will move the device tree to its final place
 * in memory if necessary, and update the global dtb pointer accordingly.
 *
 * GNU g++ quirk note: if you include a prototype here like:
 * void dtb_setup()
 *
 * with the implementation in arch-dtb.cc containing the "constructor"
 * attribute, the result is that dtb_setup ends up in the init array,
 * but _with the wrong priority_
 */

/* size_t dtb_get_phys_memory(void **addr)
 *
 * puts the physical memory address in *addr, and
 * returns the size in bytes, or 0 to signal an error.
 */
size_t dtb_get_phys_memory(u64 *addr);

#endif /* ARCH_DTB_HH */
