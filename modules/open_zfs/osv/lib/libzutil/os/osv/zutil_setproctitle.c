// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv zutil_setproctitle.c - stub for zfs_setproctitle() on OSv.
 * OSv has no /proc; setting the process title is a no-op.
 */

void
zfs_setproctitle_init(int argc, char *argv[], char *envp[])
{
	(void) argc; (void) argv; (void) envp;
}

void
zfs_setproctitle(const char *fmt, ...)
{
	(void) fmt;
}
