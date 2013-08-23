/* dumbed down version of ztest for bringup */
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012 Martin Matuska <mm@FreeBSD.org>.  All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/spa_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_scan.h>
#include <sys/zio_checksum.h>
#include <sys/refcount.h>
#include <sys/zfeature.h>

typedef struct ztest_shared_opts {
	char zo_pool[MAXNAMELEN];
	char zo_dir[MAXNAMELEN];
	char zo_alt_ztest[MAXNAMELEN];
	char zo_alt_libpath[MAXNAMELEN];
	uint64_t zo_vdevs;
	uint64_t zo_vdevtime;
	size_t zo_vdev_size;
	int zo_ashift;
	int zo_mirrors;
	int zo_raidz;
	int zo_raidz_parity;
	int zo_datasets;
	int zo_threads;
	uint64_t zo_passtime;
	uint64_t zo_killrate;
	int zo_verbose;
	int zo_init;
	uint64_t zo_time;
	uint64_t zo_maxloops;
	uint64_t zo_metaslab_gang_bang;
} ztest_shared_opts_t;

static ztest_shared_opts_t ztest_opts = {
	.zo_pool = { 'z', 't', 'e', 's', 't', '\0' },
	.zo_dir = { '/', 't', 'm', 'p', '\0' },
	.zo_alt_ztest = { '\0' },
	.zo_alt_libpath = { '\0' },
	.zo_vdevs = 5,
	.zo_ashift = SPA_MINBLOCKSHIFT,
	.zo_mirrors = 2,
	.zo_raidz = 4,
	.zo_raidz_parity = 1,
	.zo_vdev_size = SPA_MINDEVSIZE,
	.zo_datasets = 7,
	.zo_threads = 23,
	.zo_passtime = 60,		/* 60 seconds */
	.zo_killrate = 70,		/* 70% kill rate */
	.zo_verbose = 0,
	.zo_init = 1,
	.zo_time = 300,			/* 5 minutes */
	.zo_maxloops = 50,		/* max loops during spa_freeze() */
	.zo_metaslab_gang_bang = 32 << 10
};

static nvlist_t *
make_vdev_disk(char *path)
{
	nvlist_t *disk;

	VERIFY(nvlist_alloc(&disk, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(disk, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DISK) == 0);
	VERIFY(nvlist_add_string(disk, ZPOOL_CONFIG_PATH, path) == 0);
	VERIFY(nvlist_add_uint64(disk, ZPOOL_CONFIG_ASHIFT, SPA_MINBLOCKSHIFT) == 0);

	return (disk);
}

static nvlist_t *
make_vdev_root(void)
{
	nvlist_t *root, **child;
	int c;

	//ASSERT(t > 0);

	child = calloc(1, sizeof (nvlist_t *));
	child[0] = make_vdev_disk("/dev/vblk1");
	VERIFY(nvlist_add_uint64(child[0], ZPOOL_CONFIG_IS_LOG, 0) == 0);

	VERIFY(nvlist_alloc(&root, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN, child, 1) == 0);

	nvlist_free(child[0]);
	free(child);

	return (root);
}

static nvlist_t *
make_random_props()
{
	nvlist_t *props;

	VERIFY(nvlist_alloc(&props, NV_UNIQUE_NAME, 0) == 0);
	return (props);
}

int main(int argc, char **argv)
{
	nvlist_t *nvroot, *props;
	spa_t *spa;

	(void) spa_destroy(ztest_opts.zo_pool);

	/*
	 * Create the storage pool.
	 */
	nvroot = make_vdev_root();
	props = make_random_props();
	for (int i = 0; i < SPA_FEATURES; i++) {
		char buf[1024];
		(void) snprintf(buf, sizeof (buf), "feature@%s",
		    spa_feature_table[i].fi_uname);
		VERIFY3U(0, ==, nvlist_add_uint64(props, buf, 0));
	}
	VERIFY3U(0, ==, spa_create(ztest_opts.zo_pool, nvroot, props,
	    NULL, NULL));
	nvlist_free(nvroot);

	VERIFY3U(0, ==, spa_open(ztest_opts.zo_pool, &spa, FTAG));
	spa_close(spa, FTAG);
	return 0;
}
