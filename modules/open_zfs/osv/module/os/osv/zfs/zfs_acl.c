// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * Minimal ZFS ACL implementation for OSv.
 * OSv uses a simplified permission model without NFSv4 ACLs.
 * These stubs allow the ZFS code to compile and run with
 * basic permission checks.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/vnode.h>
#include <sys/dmu.h>
#include <sys/sa.h>

/*
 * Determine whether the acl data locator resides in the bonus buffer
 * or a separate SA region.
 */
void
zfs_acl_data_locator(void **dataptr, uint32_t *length, uint32_t buflen,
    boolean_t start, void *userdata)
{
	(void) dataptr; (void) length; (void) buflen;
	(void) start; (void) userdata;
}

int
zfs_acl_node_read(struct znode *zp, boolean_t have_lock, zfs_acl_t **aclpp,
    boolean_t will_modify)
{
	(void) zp; (void) have_lock; (void) aclpp; (void) will_modify;
	return (SET_ERROR(ENOTSUP));
}

void
zfs_acl_xform(znode_t *zp, zfs_acl_t *aclp, cred_t *cr)
{
	(void) zp; (void) aclp; (void) cr;
}

uint64_t
zfs_external_acl(znode_t *zp)
{
	(void) zp;
	return (0);
}

int
zfs_getacl(znode_t *zp, vsecattr_t *vsecp, boolean_t skipaclchk, cred_t *cr)
{
	(void) zp; (void) vsecp; (void) skipaclchk; (void) cr;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_setacl(znode_t *zp, vsecattr_t *vsecp, boolean_t skipaclchk, cred_t *cr)
{
	(void) zp; (void) vsecp; (void) skipaclchk; (void) cr;
	return (SET_ERROR(ENOTSUP));
}

/*
 * Simplified access check - allow everything for now.
 * A proper implementation would check POSIX permissions.
 */
int
zfs_zaccess(znode_t *zp, int mode, int flags, boolean_t skipaclchk, cred_t *cr,
    zidmap_t *mnt_ns)
{
	(void) zp; (void) mode; (void) flags;
	(void) skipaclchk; (void) cr; (void) mnt_ns;
	return (0);
}

int
zfs_zaccess_rwx(znode_t *zp, mode_t mode, int flags, cred_t *cr,
    zidmap_t *mnt_ns)
{
	(void) zp; (void) mode; (void) flags; (void) cr; (void) mnt_ns;
	return (0);
}

/*
 * Allocate a new ACL structure.
 */
zfs_acl_t *
zfs_acl_alloc(int vers)
{
	zfs_acl_t *aclp;

	aclp = kmem_zalloc(sizeof (zfs_acl_t), KM_SLEEP);
	list_create(&aclp->z_acl, sizeof (zfs_acl_node_t),
	    offsetof(zfs_acl_node_t, z_next));
	aclp->z_version = vers;
	/* OSv: no ACE iteration ops needed */
	return (aclp);
}

/*
 * Allocate an ACL node with optional ACE data buffer.
 */
zfs_acl_node_t *
zfs_acl_node_alloc(size_t bytes)
{
	zfs_acl_node_t *aclnode;

	aclnode = kmem_zalloc(sizeof (zfs_acl_node_t), KM_SLEEP);
	if (bytes) {
		aclnode->z_acldata = kmem_zalloc(bytes, KM_SLEEP);
		aclnode->z_allocdata = aclnode->z_acldata;
		aclnode->z_allocsize = bytes;
		aclnode->z_size = bytes;
	}
	return (aclnode);
}

static void
zfs_acl_node_free(zfs_acl_node_t *aclnode)
{
	if (aclnode->z_allocsize)
		kmem_free(aclnode->z_allocdata, aclnode->z_allocsize);
	kmem_free(aclnode, sizeof (zfs_acl_node_t));
}

static void
zfs_acl_release_nodes(zfs_acl_t *aclp)
{
	zfs_acl_node_t *aclnode;

	while ((aclnode = list_head(&aclp->z_acl)) != NULL) {
		list_remove(&aclp->z_acl, aclnode);
		zfs_acl_node_free(aclnode);
	}
	aclp->z_acl_bytes = 0;
	aclp->z_acl_count = 0;
}

/*
 * Free an ACL structure and all of its ACE nodes.
 */
void
zfs_acl_free(zfs_acl_t *aclp)
{
	zfs_acl_release_nodes(aclp);
	list_destroy(&aclp->z_acl);
	kmem_free(aclp, sizeof (zfs_acl_t));
}

/*
 * Simplified ACL IDs initialisation for OSv.
 *
 * OSv has no NFSv4 ACL inheritance, no FUIDs, no per-user quotas.
 * We create a trivial ACL (ZFS_ACL_TRIVIAL, no ACEs) and derive
 * the mode directly from vap->va_mode.
 */
int
zfs_acl_ids_create(znode_t *dzp, int flag, vattr_t *vap, cred_t *cr,
    vsecattr_t *vsecp, zfs_acl_ids_t *acl_ids, zidmap_t *mnt_ns)
{
	(void) dzp; (void) flag; (void) cr; (void) vsecp; (void) mnt_ns;

	memset(acl_ids, 0, sizeof (zfs_acl_ids_t));
	/*
	 * Include the file type bits from va_type.  OSv's sys_open() ORs
	 * in S_IFREG and sys_mkdir() ORs in S_IFDIR before calling VOP_CREATE/
	 * VOP_MKDIR, but sys_symlink() does not add S_IFLNK to va_mode.
	 * Using MAKEIMODE ensures the type bits are always present so that
	 * IFTOVT(z_mode) returns the correct vtype when the znode is loaded.
	 */
	acl_ids->z_mode = MAKEIMODE(vap->va_type,
	    (vap->va_mask & AT_MODE) ? vap->va_mode : 0755);
	acl_ids->z_fuid = 0;
	acl_ids->z_fgid = 0;
	acl_ids->z_aclp = zfs_acl_alloc(ZFS_ACL_VERSION_FUID);
	acl_ids->z_aclp->z_hints = ZFS_ACL_TRIVIAL;
	acl_ids->z_aclp->z_acl_count = 0;
	acl_ids->z_aclp->z_acl_bytes = 0;
	acl_ids->z_fuidp = NULL;
	return (0);
}

/*
 * Free ACL and fuid_info stored in acl_ids (but not acl_ids itself).
 */
void
zfs_acl_ids_free(zfs_acl_ids_t *acl_ids)
{
	if (acl_ids->z_aclp != NULL) {
		zfs_acl_free(acl_ids->z_aclp);
		acl_ids->z_aclp = NULL;
	}
	acl_ids->z_fuidp = NULL;
}

/*
 * OSv has no per-user or per-project quotas.
 */
boolean_t
zfs_acl_ids_overquota(zfsvfs_t *zv, zfs_acl_ids_t *acl_ids, uint64_t projid)
{
	(void) zv; (void) acl_ids; (void) projid;
	return (B_FALSE);
}

boolean_t
zfs_has_access(znode_t *zp, cred_t *cr)
{
	(void) zp; (void) cr;
	return (B_TRUE);
}

int
zfs_zaccess_unix(void *zp, int mode, cred_t *cr)
{
	(void) zp; (void) mode; (void) cr;
	return (0);
}

int
zfs_acl_access(znode_t *zp, int mode, cred_t *cr)
{
	(void) zp; (void) mode; (void) cr;
	return (0);
}

int
zfs_zaccess_delete(znode_t *dzp, znode_t *zp, cred_t *cr, zidmap_t *mnt_ns)
{
	(void) dzp; (void) zp; (void) cr; (void) mnt_ns;
	return (0);
}

int
zfs_zaccess_rename(znode_t *sdzp, znode_t *szp, znode_t *tdzp,
    znode_t *tzp, cred_t *cr, zidmap_t *mnt_ns)
{
	(void) sdzp; (void) szp; (void) tdzp; (void) tzp; (void) cr;
	(void) mnt_ns;
	return (0);
}
