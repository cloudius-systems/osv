// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2026 OSv contributors
 * All rights reserved.
 *
 * OSv SPL thread - stub for OpenZFS compatibility
 */

#ifndef _SPL_THREAD_H_
#define	_SPL_THREAD_H_

/*
 * OSv doesn't expose thread names/IDs in the same way.
 * These macros provide stubs for ZFS logging/debugging.
 * Note: We can't override getpid() since OSv's unistd.h declares it.
 */
#define	getcomm() "osv"

#endif /* _SPL_THREAD_H_ */
