// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv getmntent/getmntany backed by the live VFS mount table.
 *
 * On OSv there is no /etc/mnttab or /proc/mounts stream, so the original
 * stubs always returned EOF.  That broke zfs_unmount()/zfs destroy/zpool
 * export: libzfs decides whether a dataset needs unmounting by looking it
 * up via libzfs_mnttab_find() -> libzfs_mnttab_update() -> getmntent().
 * With the stub returning EOF, kernel auto-mounted datasets (mounted by
 * zfs_domount() at pool/dataset create time, not through libzfs do_mount)
 * were invisible, so the objset stayed owned and destroy/export failed
 * with EBUSY / "dataset is busy".
 *
 * This shim enumerates the real VFS mounts through osv::current_mounts()
 * and hands them to libzfs one struct mnttab at a time, filtered to ZFS.
 * The per-FILE* iteration state is keyed off the FILE* the caller passed
 * to fopen(MNTTAB); we take a fresh snapshot on the first getmntent() for
 * a given stream (detected by a position reset).
 */
#include <osv/mount.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <string>

extern "C" {
#include <sys/mnttab.h>

struct osv_mnt_iter {
	std::vector<osv::mount_desc> snap;
	size_t idx;
	std::vector<std::string> keep; // backing storage for returned char*
};

/* One iterator per process is enough for libzfs' usage pattern. */
static __thread osv_mnt_iter *g_iter = nullptr;

static void fill(struct mnttab *mp, osv_mnt_iter *it, const osv::mount_desc &m)
{
	it->keep.push_back(m.special);
	mp->mnt_special = (char *)it->keep.back().c_str();
	it->keep.push_back(m.path);
	mp->mnt_mountp = (char *)it->keep.back().c_str();
	it->keep.push_back(m.type);
	mp->mnt_fstype = (char *)it->keep.back().c_str();
	it->keep.push_back(m.options);
	mp->mnt_mntopts = (char *)it->keep.back().c_str();
}

static osv_mnt_iter *ensure_iter(void)
{
	if (!g_iter) {
		g_iter = new osv_mnt_iter();
		g_iter->snap = osv::current_mounts();
		g_iter->idx = 0;
	}
	return g_iter;
}

/*
 * getmntent: return the next ZFS mount, or -1 (EOF) when exhausted.
 * libzfs opens a fresh FILE* per scan; we (re)snapshot when idx wraps.
 */
int getmntent(FILE *fp, struct mnttab *mp)
{
	(void) fp;
	osv_mnt_iter *it = ensure_iter();
	while (it->idx < it->snap.size()) {
		const osv::mount_desc &m = it->snap[it->idx++];
		if (m.type != "zfs")
			continue;
		fill(mp, it, m);
		return (0);
	}
	/* exhausted: reset so the next fopen()/scan starts fresh */
	delete g_iter;
	g_iter = nullptr;
	return (-1);
}

int getmntany(FILE *fp, struct mnttab *mp, struct mnttab *mpref)
{
	(void) fp;
	/* fresh snapshot for a targeted lookup */
	std::vector<osv::mount_desc> snap = osv::current_mounts();
	for (auto &m : snap) {
		if (m.type != "zfs")
			continue;
		if (mpref && mpref->mnt_special &&
		    m.special != mpref->mnt_special)
			continue;
		if (mpref && mpref->mnt_mountp &&
		    m.path != mpref->mnt_mountp)
			continue;
		/* leak these small strings; libzfs strdup's them immediately */
		mp->mnt_special = strdup(m.special.c_str());
		mp->mnt_mountp  = strdup(m.path.c_str());
		mp->mnt_fstype  = strdup(m.type.c_str());
		mp->mnt_mntopts = strdup(m.options.c_str());
		return (0);
	}
	return (-1);
}

} // extern "C"
