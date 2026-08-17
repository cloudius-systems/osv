// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv entry point for zpool.so
 *
 * The upstream zpool_main.c compiles 'main' with HIDDEN ELF visibility
 * in the OSv build environment (due to glibc-compat headers in the
 * include path).  OSv's ELF loader looks up "main" via the dynamic
 * symbol table (.dynsym), which only contains DEFAULT-visibility symbols.
 *
 * Fix: rename the upstream main to zpool_real_main (via -Dmain=zpool_real_main
 * on the zpool-cmd-objects compilation) and re-export it here with explicit
 * DEFAULT visibility so the linker places it in .dynsym.
 */

extern int zpool_real_main(int argc, char **argv);

__attribute__((visibility("default")))
int main(int argc, char **argv)
{
	return zpool_real_main(argc, argv);
}
