// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv userspace zfs_context_os.h
 *
 * Minimal definitions for building libzfs/libzutil/zpool/zfs userspace
 * tools against OpenZFS 2.4.1 on OSv.  This is NOT the kernel version
 * (include/os/osv/zfs/sys/zfs_context_os.h); it is the userspace SPL
 * context header placed so that the libspl include path finds it at
 * sys/zfs_context_os.h.
 */

#ifndef ZFS_CONTEXT_OS_H
#define	ZFS_CONTEXT_OS_H

/*
 * OSv unikernel: no kernel/user split, large stacks available.
 */
#define	HAVE_LARGE_STACKS	1

#endif /* ZFS_CONTEXT_OS_H */
