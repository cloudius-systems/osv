// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZVOL OS-specific operations for OSv.
 *
 * ZVOLs (ZFS volumes) present block devices backed by ZFS datasets.
 * OSv does not currently use ZVOLs, so these are all stubs.
 * They can be implemented later if block-device-backed ZFS volumes
 * are needed.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/zil.h>
#include <sys/zio.h>
#include <sys/spa.h>
#include <sys/zfs_rlock.h>
#include <sys/dataset_kstats.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>

void
zvol_os_free(zvol_state_t *zv)
{
	(void) zv;
}

int
zvol_os_rename_minor(zvol_state_t *zv, const char *newname)
{
	(void) zv;
	(void) newname;
	return (0);
}

int
zvol_os_create_minor(const char *name)
{
	(void) name;
	return (SET_ERROR(ENOTSUP));
}

int
zvol_os_update_volsize(zvol_state_t *zv, uint64_t volsize)
{
	(void) zv;
	(void) volsize;
	return (SET_ERROR(ENOTSUP));
}

boolean_t
zvol_os_is_zvol(const char *path)
{
	(void) path;
	return (B_FALSE);
}

void
zvol_os_clear_private(zvol_state_t *zv)
{
	(void) zv;
}

void
zvol_os_set_disk_ro(zvol_state_t *zv, int flags)
{
	(void) zv;
	(void) flags;
}

void
zvol_os_set_capacity(zvol_state_t *zv, uint64_t capacity)
{
	(void) zv;
	(void) capacity;
}

void
zvol_os_remove_minor(zvol_state_t *zv)
{
	(void) zv;
}

void
zvol_wait_close(zvol_state_t *zv)
{
	(void) zv;
}

int
zvol_init(void)
{
	return (zvol_init_impl());
}

void
zvol_fini(void)
{
	zvol_fini_impl();
}
