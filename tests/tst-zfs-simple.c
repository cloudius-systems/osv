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

static char ztest_dev_template[] = "%s/%s.%llua";
static char ztest_aux_template[] = "%s/%s.%s.%llu";
	
uint64_t	zs_vdev_next_leaf;
uint64_t	zs_vdev_aux;
int		zs_mirrors;

static void
fatal(int do_perror, char *message, ...)
{
#define	FATAL_MSG_SZ	1024
	va_list args;
	int save_errno = errno;
	char buf[FATAL_MSG_SZ];

	(void) fflush(stdout);

	va_start(args, message);
	(void) sprintf(buf, "ztest: ");
	/* LINTED */
	(void) vsprintf(buf + strlen(buf), message, args);
	va_end(args);
	if (do_perror) {
		(void) snprintf(buf + strlen(buf), FATAL_MSG_SZ - strlen(buf),
		    ": %s", strerror(save_errno));
	}
	(void) fprintf(stderr, "%s\n", buf);
	exit(3);
}

static uint64_t
ztest_get_ashift(void)
{
	return (SPA_MINBLOCKSHIFT);
}


static nvlist_t *
make_vdev_file(char *path, char *aux, char *pool, size_t size, uint64_t ashift)
{
	char pathbuf[MAXPATHLEN];
	uint64_t vdev;
	nvlist_t *file;

	if (ashift == 0)
		ashift = ztest_get_ashift();

	if (path == NULL) {
		path = pathbuf;

		if (aux != NULL) {
			vdev = zs_vdev_aux;
			(void) snprintf(path, sizeof (pathbuf),
			    ztest_aux_template, ztest_opts.zo_dir,
			    pool == NULL ? ztest_opts.zo_pool : pool,
			    aux, vdev);
		} else {
			vdev = zs_vdev_next_leaf++;
			(void) snprintf(path, sizeof (pathbuf),
			    ztest_dev_template, ztest_opts.zo_dir,
			    pool == NULL ? ztest_opts.zo_pool : pool, vdev);
		}
	}

	if (size != 0) {
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd == -1)
			fatal(1, "can't open %s", path);
#if 0
		if (ftruncate(fd, size) != 0)
			fatal(1, "can't ftruncate %s", path);
#else
		char buf = '0';
		if (pwrite(fd, &buf, 1, size-1) != 1)
			fatal(1, "can't extend %s", path);
#endif
		(void) close(fd);
	}

	VERIFY(nvlist_alloc(&file, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(file, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE) == 0);
	VERIFY(nvlist_add_string(file, ZPOOL_CONFIG_PATH, path) == 0);
	VERIFY(nvlist_add_uint64(file, ZPOOL_CONFIG_ASHIFT, ashift) == 0);

	return (file);
}

static nvlist_t *
make_vdev_raidz(char *path, char *aux, char *pool, size_t size,
    uint64_t ashift, int r)
{
	nvlist_t *raidz, **child;
	int c;

	if (r < 2)
		return (make_vdev_file(path, aux, pool, size, ashift));
	child = calloc(r, sizeof (nvlist_t *));

	for (c = 0; c < r; c++)
		child[c] = make_vdev_file(path, aux, pool, size, ashift);

	VERIFY(nvlist_alloc(&raidz, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(raidz, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_RAIDZ) == 0);
	VERIFY(nvlist_add_uint64(raidz, ZPOOL_CONFIG_NPARITY,
	    ztest_opts.zo_raidz_parity) == 0);
	VERIFY(nvlist_add_nvlist_array(raidz, ZPOOL_CONFIG_CHILDREN,
	    child, r) == 0);

	for (c = 0; c < r; c++)
		nvlist_free(child[c]);

	free(child);

	return (raidz);
}

static nvlist_t *
make_vdev_mirror(char *path, char *aux, char *pool, size_t size,
    uint64_t ashift, int r, int m)
{
	nvlist_t *mirror, **child;
	int c;

	if (m < 1)
		return (make_vdev_raidz(path, aux, pool, size, ashift, r));

	child = calloc(m, sizeof (nvlist_t *));

	for (c = 0; c < m; c++)
		child[c] = make_vdev_raidz(path, aux, pool, size, ashift, r);

	VERIFY(nvlist_alloc(&mirror, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(mirror, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_MIRROR) == 0);
	VERIFY(nvlist_add_nvlist_array(mirror, ZPOOL_CONFIG_CHILDREN,
	    child, m) == 0);

	for (c = 0; c < m; c++)
		nvlist_free(child[c]);

	free(child);

	return (mirror);
}

static nvlist_t *
make_vdev_root(char *path, char *aux, char *pool, size_t size, uint64_t ashift,
    int log, int r, int m, int t)
{
	nvlist_t *root, **child;
	int c;

	ASSERT(t > 0);

	child = calloc(t, sizeof (nvlist_t *));

	for (c = 0; c < t; c++) {
		child[c] = make_vdev_mirror(path, aux, pool, size, ashift,
		    r, m);
		VERIFY(nvlist_add_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    log) == 0);
	}

	VERIFY(nvlist_alloc(&root, NV_UNIQUE_NAME, 0) == 0);
	VERIFY(nvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_nvlist_array(root, aux ? aux : ZPOOL_CONFIG_CHILDREN,
	    child, t) == 0);

	for (c = 0; c < t; c++)
		nvlist_free(child[c]);

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
	zs_vdev_next_leaf = 0;
//	zs->zs_splits = 0;
//	zs->zs_mirrors = ztest_opts.zo_mirrors;
	nvroot = make_vdev_root(NULL, NULL, NULL, ztest_opts.zo_vdev_size, 0,
	    0, ztest_opts.zo_raidz, zs_mirrors, 1);
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
//	zs->zs_metaslab_sz =
//	    1ULL << spa->spa_root_vdev->vdev_child[0]->vdev_ms_shift;

	spa_close(spa, FTAG);
}
