// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv libshare SMB stub.
 *
 * OSv is a unikernel with no SMB server capability.  All sharing
 * operations are stubs that return SA_OK (success/no-op).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <libshare.h>
#include <libshare_impl.h>

static int
osv_smb_validate_shareopts(const char *shareopts)
{
	(void) shareopts;
	return (SA_OK);
}

static int
osv_smb_enable_share(sa_share_impl_t impl_share)
{
	(void) impl_share;
	return (SA_OK);
}

static int
osv_smb_disable_share(sa_share_impl_t impl_share)
{
	(void) impl_share;
	return (SA_OK);
}

static boolean_t
osv_smb_is_shared(sa_share_impl_t impl_share)
{
	(void) impl_share;
	return (B_FALSE);
}

static int
osv_smb_commit_shares(void)
{
	return (SA_OK);
}

static void
osv_smb_truncate_shares(void)
{
}

const sa_fstype_t libshare_smb_type = {
	.enable_share		= osv_smb_enable_share,
	.disable_share		= osv_smb_disable_share,
	.is_shared		= osv_smb_is_shared,
	.validate_shareopts	= osv_smb_validate_shareopts,
	.commit_shares		= osv_smb_commit_shares,
	.truncate_shares	= osv_smb_truncate_shares,
};
