/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Shared definitions for the preadv2/pwritev2 (RWF_*) and renameat2 (RENAME_*)
// flags and prototypes, so the implementation (fs/vfs/main.cc) and the tests
// use one authoritative copy that cannot drift out of sync.  These mirror the
// Linux <fcntl.h>/<stdio.h> values; guarded so a libc that already provides
// them wins.

#ifndef _OSV_FS_FLAGS_H_
#define _OSV_FS_FLAGS_H_

#include <sys/types.h>
#include <sys/uio.h>

#ifndef RWF_HIPRI
#define RWF_HIPRI  0x00000001   /* high priority request */
#define RWF_DSYNC  0x00000002   /* per-io O_DSYNC */
#define RWF_SYNC   0x00000004   /* per-io O_SYNC */
#define RWF_NOWAIT 0x00000008   /* per-io nonblocking mode */
#define RWF_APPEND 0x00000010   /* per-io O_APPEND */
#endif

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)   /* don't overwrite target */
#define RENAME_EXCHANGE  (1 << 1)   /* exchange source and dest */
#define RENAME_WHITEOUT  (1 << 2)   /* whiteout source */
#endif

#ifdef __cplusplus
extern "C" {
#endif

ssize_t preadv2(int fd, const struct iovec *iov, int iovcnt, off_t offset, int flags);
ssize_t pwritev2(int fd, const struct iovec *iov, int iovcnt, off_t offset, int flags);
int renameat2(int olddirfd, const char *oldpath, int newdirfd,
              const char *newpath, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif /* _OSV_FS_FLAGS_H_ */
