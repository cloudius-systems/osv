/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FCNTL_H
#define OSV_FCNTL_H

#include <sys/cdefs.h>
#include <sys/types.h>
#include <fcntl.h>

__BEGIN_DECLS

/*
 * Kernel encoding of open mode; separate read and write bits that are
 * independently testable: 1 greater than the above.
 */
#define FREAD           0x00000001
#define FWRITE          0x00000002
#define FEXCL           O_EXCL
#define F_FREESP        0x00000008
#define F_KEEPSP        0x00000010

/* Fallocate modes */
#define FALLOC_FL_KEEP_SIZE 1
#define FALLOC_FL_PUNCH_HOLE 2

#define loff_t off_t
typedef struct flock64 {
    short l_type;
    short l_whence;
    loff_t l_start;
    loff_t l_len;
    pid_t l_pid;
} flock64_t;

/* convert from open() flags to/from fflags; convert O_RD/WR to FREAD/FWRITE */
static inline int fflags(int oflags)
{
    int rw = oflags & O_ACCMODE;
    oflags &= ~O_ACCMODE;
    return (rw + 1) | oflags;
}

static inline int oflags(int fflags)
{
    int rw = fflags & (FREAD|FWRITE);
    fflags &= ~(FREAD|FWRITE);
    return (rw - 1) | fflags;
}

__END_DECLS

#endif /* !OSV_FCNTL_H */
