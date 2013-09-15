/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_FS_UNSUPPORTED_H
#define INCLUDED_FS_UNSUPPORTED_H

#include <sys/cdefs.h>

#include <osv/file.h>

__BEGIN_DECLS

fo_rdwr_t unsupported_read;
fo_rdwr_t unsupported_write;
fo_truncate_t unsupported_truncate;
fo_ioctl_t unsupported_ioctl;
fo_stat_t unsupported_stat;
fo_chmod_t unsupported_chmod;
fo_poll_t unsupported_poll;

__END_DECLS

#endif /* INCLUDED_FS_UNSUPPORTED_H */
