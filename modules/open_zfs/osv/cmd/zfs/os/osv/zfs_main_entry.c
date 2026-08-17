// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv entry point for zfs.so
 *
 * Same visibility fix as zpool_main_entry.c - see that file for details.
 * The upstream zfs_main.c 'main' is renamed to zfs_real_main and
 * re-exported here with DEFAULT visibility.
 */

extern int zfs_real_main(int argc, char **argv);

__attribute__((visibility("default")))
int main(int argc, char **argv)
{
	return zfs_real_main(argc, argv);
}
