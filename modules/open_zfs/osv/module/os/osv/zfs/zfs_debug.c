// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS debug message infrastructure for OSv.
 * Based on the FreeBSD version.
 */

#include <sys/zfs_context.h>
#include <sys/kstat.h>

typedef struct zfs_dbgmsg {
	list_node_t zdm_node;
	time_t zdm_timestamp;
	uint_t zdm_size;
	char zdm_msg[];
} zfs_dbgmsg_t;

static list_t zfs_dbgmsgs;
static uint_t zfs_dbgmsg_size = 0;
static kmutex_t zfs_dbgmsgs_lock;
uint_t zfs_dbgmsg_maxsize = 4<<20; /* 4MB */

int zfs_dbgmsg_enable = B_TRUE;

static void
zfs_dbgmsg_purge(uint_t max_size)
{
	zfs_dbgmsg_t *zdm;
	uint_t size;

	ASSERT(MUTEX_HELD(&zfs_dbgmsgs_lock));

	while (zfs_dbgmsg_size > max_size) {
		zdm = list_remove_head(&zfs_dbgmsgs);
		if (zdm == NULL)
			return;

		size = zdm->zdm_size;
		kmem_free(zdm, size);
		zfs_dbgmsg_size -= size;
	}
}

void
zfs_dbgmsg_init(void)
{
	list_create(&zfs_dbgmsgs, sizeof (zfs_dbgmsg_t),
	    offsetof(zfs_dbgmsg_t, zdm_node));
	mutex_init(&zfs_dbgmsgs_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
zfs_dbgmsg_fini(void)
{
	mutex_enter(&zfs_dbgmsgs_lock);
	zfs_dbgmsg_purge(0);
	mutex_exit(&zfs_dbgmsgs_lock);
	mutex_destroy(&zfs_dbgmsgs_lock);
}

void
__zfs_dbgmsg(char *buf)
{
	zfs_dbgmsg_t *zdm;
	uint_t size;

	size = sizeof (zfs_dbgmsg_t) + strlen(buf) + 1;
	zdm = kmem_zalloc(size, KM_SLEEP);
	zdm->zdm_size = size;
	zdm->zdm_timestamp = gethrestime_sec();
	strcpy(zdm->zdm_msg, buf);

	mutex_enter(&zfs_dbgmsgs_lock);
	list_insert_tail(&zfs_dbgmsgs, zdm);
	zfs_dbgmsg_size += size;
	zfs_dbgmsg_purge(zfs_dbgmsg_maxsize);
	mutex_exit(&zfs_dbgmsgs_lock);
}

void
__set_error(const char *file, const char *func, int line, int err)
{
	if (zfs_flags & ZFS_DEBUG_SET_ERROR)
		__dprintf(B_FALSE, file, func, line, "error %lu",
		    (ulong_t)err);
}

void
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
{
	const char *newfile;
	va_list adx;
	size_t size;
	char *buf;
	char *nl;
	int i;

	size = 1024;
	buf = kmem_alloc(size, KM_SLEEP);

	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1;
	} else {
		newfile = file;
	}

	i = snprintf(buf, size, "%s:%d:%s(): ", newfile, line, func);

	if (i < size) {
		va_start(adx, fmt);
		(void) vsnprintf(buf + i, size - i, fmt, adx);
		va_end(adx);
	}

	if (dprint && buf[0] != '\0') {
		nl = &buf[strlen(buf) - 1];
		if (*nl == '\n')
			*nl = '\0';
	}

	__zfs_dbgmsg(buf);

	kmem_free(buf, size);
}
