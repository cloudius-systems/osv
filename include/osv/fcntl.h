/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FCNTL_H
#define OSV_FCNTL_H

#include <sys/cdefs.h>
#include <fcntl.h>

__BEGIN_DECLS

/*
 * Kernel encoding of open mode; separate read and write bits that are
 * independently testable: 1 greater than the above.
 */
#define FREAD           0x00000001
#define FWRITE          0x00000002
#define FEXCL		O_EXCL

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
