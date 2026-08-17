// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv libshare NFS stub.
 *
 * OSv is a unikernel with no NFS/SMB server capability.  All sharing
 * operations are stubs that return SA_OK (success/no-op).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <libshare.h>
#include <libshare_impl.h>
#include <nfs.h>

static int
osv_nfs_validate_shareopts(const char *shareopts)
{
	(void) shareopts;
	return (SA_OK);
}

static int
osv_nfs_enable_share(sa_share_impl_t impl_share)
{
	(void) impl_share;
	return (SA_OK);
}

static int
osv_nfs_disable_share(sa_share_impl_t impl_share)
{
	(void) impl_share;
	return (SA_OK);
}

static boolean_t
osv_nfs_is_shared(sa_share_impl_t impl_share)
{
	(void) impl_share;
	return (B_FALSE);
}

static int
osv_nfs_commit_shares(void)
{
	return (SA_OK);
}

static void
osv_nfs_truncate_shares(void)
{
}

const sa_fstype_t libshare_nfs_type = {
	.enable_share		= osv_nfs_enable_share,
	.disable_share		= osv_nfs_disable_share,
	.is_shared		= osv_nfs_is_shared,
	.validate_shareopts	= osv_nfs_validate_shareopts,
	.commit_shares		= osv_nfs_commit_shares,
	.truncate_shares	= osv_nfs_truncate_shares,
};
