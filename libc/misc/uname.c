/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/utsname.h>
#include <string.h>

// The Linux version we're pretending to be
#define LINUX_MAJOR 3
#define LINUX_MINOR 7
#define LINUX_PATCH 0

// Verify that the version defined in <linux/version.h> is the same
#include <linux/version.h>
_Static_assert(KERNEL_VERSION(LINUX_MAJOR, LINUX_MINOR, LINUX_PATCH)
        == LINUX_VERSION_CODE,
        "LINUX_VERSION_CODE in include/glibc-compat/linux/version.h "
        "does not match version in libc/misc/uname.c");

#define str(s) #s
#define str2(s) str(s)

struct utsname utsname = {
	.sysname	= "Linux",	/* lie, to avoid confusing the payload. */
	.nodename	= "osv.local",
	.release	= str2(LINUX_MAJOR) "." str2(LINUX_MINOR) "." str2(LINUX_PATCH),
	.version	= "#1 SMP",
	.machine	= "x86_64",
};

int uname(struct utsname *uts)
{
	memcpy(uts, &utsname, sizeof(struct utsname));
	return 0;
}
