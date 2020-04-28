/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _VFS_ID_H
#define _VFS_ID_H

#define RAMFS_ID	(1ULL<<56)
#define DEVFS_ID	(2ULL<<56)
#define NFS_ID		(3ULL<<56)
#define PROCFS_ID	(4ULL<<56)
#define SYSFS_ID	(5ULL<<56)
#define ZFS_ID		(6ULL<<56)
#define ROFS_ID		(7ULL<<56)
#define VIRTIOFS_ID	(8ULL<<56)

#endif /* !_VFS_ID_H */
