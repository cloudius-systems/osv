/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/*
 * The OSv version of this file is dumbed down to a minimum as we assume
 * single user operation.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/priv.h>
#include <sys/vnode.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/policy.h>

int
secpolicy_nfs(cred_t *cr)
{

	return (priv_check_cred(cr, PRIV_NFS_DAEMON, 0));
}

int
secpolicy_zfs(cred_t *cr)
{

	return (0);
}

int
secpolicy_sys_config(cred_t *cr, int checkonly __unused2)
{

	return (0);
}

int
secpolicy_zinject(cred_t *cr)
{

	return (0);
}

int
secpolicy_fs_unmount(cred_t *cr, struct mount *vfsp __unused2)
{

	return (0);
}

int
secpolicy_fs_owner(struct mount *mp, cred_t *cr)
{

	return (0);
}

/*
 * This check is done in kern_link(), so we could just return 0 here.
 */
int
secpolicy_basic_link(vnode_t *vp, cred_t *cr)
{

	return (0);
}

int
secpolicy_vnode_stky_modify(cred_t *cr)
{

	return (EPERM);
}

int
secpolicy_vnode_remove(vnode_t *vp, cred_t *cr)
{

	return (0);
}

int
secpolicy_vnode_access(cred_t *cr, vnode_t *vp, uid_t owner, accmode_t accmode)
{

	return (0);
}

/*
 * Like secpolicy_vnode_access() but we get the actual wanted mode and the
 * current mode of the file, not the missing bits.
 */
int
secpolicy_vnode_access2(cred_t *cr, vnode_t *vp, uid_t owner,
    accmode_t curmode, accmode_t wantmode)
{
	return (0);
}

int
secpolicy_vnode_any_access(cred_t *cr, vnode_t *vp, uid_t owner)
{
	return (0);
}

int
secpolicy_vnode_setdac(vnode_t *vp, cred_t *cr, uid_t owner)
{

	return (0);
}

int
secpolicy_vnode_setattr(cred_t *cr, vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, int flags,
    int unlocked_access(void *, int, cred_t *), void *node)
{
	int mask = vap->va_mask;
	int error;

	if (mask & AT_SIZE) {
		if (vp->v_type == VDIR)
			return (EISDIR);
		error = unlocked_access(node, VWRITE, cr);
		if (error)
			return (error);
	}

	return (0);
}

int
secpolicy_vnode_create_gid(cred_t *cr)
{

	return (0);
}

int
secpolicy_vnode_setids_setgids(vnode_t *vp, cred_t *cr, gid_t gid)
{

	return (0);
}

int
secpolicy_vnode_setid_retain(vnode_t *vp, cred_t *cr,
    boolean_t issuidroot __unused2)
{

	return (0);
}

void
secpolicy_setid_clear(struct vattr *vap, vnode_t *vp, cred_t *cr)
{
	return;
}

int
secpolicy_setid_setsticky_clear(vnode_t *vp, struct vattr *vap,
    const struct vattr *ovap, cred_t *cr)
{
	return (0);
}

int
secpolicy_fs_mount(cred_t *cr, vnode_t *mvp, struct mount *vfsp)
{

	return (0);
}

int
secpolicy_vnode_owner(vnode_t *vp, cred_t *cr, uid_t owner)
{

	return (0);
}

int
secpolicy_vnode_chown(vnode_t *vp, cred_t *cr, uid_t owner)
{

	return (0);
}

void
secpolicy_fs_mount_clearopts(cred_t *cr, struct mount *vfsp)
{
}

/*
 * Check privileges for setting xvattr attributes
 */
int
secpolicy_xvattr(vnode_t *vp, xvattr_t *xvap, uid_t owner, cred_t *cr,
    vtype_t vtype)
{

	return (0);
}

int
secpolicy_smb(cred_t *cr)
{
	return (0);
}
