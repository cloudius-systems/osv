
#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/buf.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "romfs.h"

extern struct vnops romfs_vnops;

static int
romfs_mount(mount_t mp, char *dev, int flags, void *data)
{
	struct romfs_dsb *sb;
	struct romfs_mount *rmp;
	struct buf *bp;
	unsigned long pos;
	size_t len;
	int ret;

	DPRINTF(("romfs_mount: dev=%s\n", dev));


	ret = bread(mp->m_dev, 0, &bp);
	if (ret) {
		printf("cannot read romfs superblock\n");
		return ret;
	}
	sb = bp->b_data;

	ret = EINVAL;
	if (sb->word0 != ROMSB_WORD0 || sb->word1 != ROMSB_WORD1) {
		printf("romfs: invalid magic\n");
		goto out_brelse;
	}

	if (ntohl(sb->size) < ROMFH_SIZE) {
		printf("romfs: image too small\n");
		goto out_brelse;
	}

	ret = ENOMEM;
	rmp = malloc(sizeof(*rmp));
	if (!rmp)
		goto out_brelse;

	rmp->rm_maxsize = ntohl(sb->size);
	brelse(bp);

	len = strnlen(sb->name, ROMFS_MAXFN);
	pos = (ROMFH_SIZE + len + 1 + ROMFH_PAD) & ROMFH_MASK;

	ret = romfs_read_node(mp->m_root, pos);
	if (ret)
		goto out_free_rmp;

	mp->m_data = rmp;
	return 0;

out_brelse:
	brelse(bp);
	return ret;
out_free_rmp:
	free(rmp);
	return ret;
}

static int
romfs_unmount(mount_t mp)
{

	return EBUSY;
}

static int
romfs_vget(mount_t mp, struct vnode *vp)
{
	struct romfs_node *np = malloc(sizeof(*np));

	if (!np)
		return ENOMEM;
	vp->v_data = np;
	return 0;
}

struct vfsops romfs_vfsops = {
	.vfs_mount	= romfs_mount,
	.vfs_unmount	= romfs_unmount,
	.vfs_sync	= (vfsop_sync_t)vfs_nullop,
	.vfs_vget	= romfs_vget,
	.vfs_statfs	= (vfsop_statfs_t)vfs_nullop,
	.vfs_vnops	= &romfs_vnops,
};
