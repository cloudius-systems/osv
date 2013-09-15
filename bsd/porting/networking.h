/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __NETWORKING_H__
#define __NETWORKING_H__

#include <sys/cdefs.h>

__BEGIN_DECLS

/* Interface Functions */
int osv_start_if(const char* if_name, const char* ip_addr, const char* mask_addr);

int osv_ifup(const char* if_name);

__END_DECLS

#endif /* __NETWORKING_H__ */
