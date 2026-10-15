/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * musl 1.2.x made h_errno thread-local by reaching into its own
 * struct pthread (upstream 9d0b8b92 "make h_errno thread-local", plus
 * cf27184d restoring the ABI of the global for ancient binaries).  OSv
 * does not use musl's thread implementation, so musl's version cannot
 * build here.  This provides the same semantics with OSv's own
 * thread-local storage: a real __thread int, plus the global that the
 * legacy ABI still needs to exist.
 */

#include <netdb.h>

#undef h_errno

/* Kept for binaries that reference the global symbol directly. */
int h_errno;

static int __thread h_errno_tls;

int *__h_errno_location(void)
{
	return &h_errno_tls;
}
