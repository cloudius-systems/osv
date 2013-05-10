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

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kstat.h>

#include <stdlib.h>
#include <osv/debug.h>
#include <bsd/porting/netport.h>

SYSCTL_NODE(, OID_AUTO, kstat, CTLFLAG_RW, 0, "Kernel statistics");

kstat_t *
kstat_create(char *module, int instance, char *name, char *class, uchar_t type,
    ulong_t ndata, uchar_t flags)
{
	struct sysctl_oid *root;
	kstat_t *ksp;

	/*
	 * Allocate the main structure. We don't need to copy module/class/name
	 * stuff in here, because it is only used for sysctl node creation
	 * done in this function.
	 */
	ksp = malloc(sizeof(*ksp));
	ksp->ks_ndata = ndata;

#if 0
	/*
	 * Create sysctl tree for those statistics:
	 *
	 *	kstat.<module>.<class>.<name>.
	 */
	sysctl_ctx_init(&ksp->ks_sysctl_ctx);
	root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_kstat), OID_AUTO, module, CTLFLAG_RW, 0,
	    "");
	if (root == NULL) {
		kprintf("%s: Cannot create kstat.%s tree!\n", __func__, module);
		sysctl_ctx_free(&ksp->ks_sysctl_ctx);
		free(ksp);
		return (NULL);
	}
	root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx, SYSCTL_CHILDREN(root),
	    OID_AUTO, class, CTLFLAG_RW, 0, "");
	if (root == NULL) {
		kprintf("%s: Cannot create kstat.%s.%s tree!\n", __func__,
		    module, class);
		sysctl_ctx_free(&ksp->ks_sysctl_ctx);
		free(ksp);
		return (NULL);
	}
	root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx, SYSCTL_CHILDREN(root),
	    OID_AUTO, name, CTLFLAG_RW, 0, "");
	if (root == NULL) {
		kprintf("%s: Cannot create kstat.%s.%s.%s tree!\n", __func__,
		    module, class, name);
		sysctl_ctx_free(&ksp->ks_sysctl_ctx);
		free(ksp);
		return (NULL);
	}
	ksp->ks_sysctl_root = root;
#endif

	return (ksp);
}

#if 0
static int
kstat_sysctl(SYSCTL_HANDLER_ARGS)
{
	kstat_named_t *ksent = arg1;
	uint64_t val;

	val = ksent->value.ui64;
	return sysctl_handle_64(oidp, &val, 0, req);
}
#endif

void
kstat_install(kstat_t *ksp)
{
#if 0
	kstat_named_t *ksent;
	u_int i;

	ksent = ksp->ks_data;
	for (i = 0; i < ksp->ks_ndata; i++, ksent++) {
		SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
		    SYSCTL_CHILDREN(ksp->ks_sysctl_root), OID_AUTO, ksent->name,
		    CTLTYPE_U64 | CTLFLAG_RD, ksent, sizeof(*ksent),
		    kstat_sysctl, "QU", "");
	}
#endif
}

void
kstat_delete(kstat_t *ksp)
{
#if 0
	sysctl_ctx_free(&ksp->ks_sysctl_ctx);
#endif
	free(ksp);
}
