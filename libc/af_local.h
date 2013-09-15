/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef AF_LOCAL_H_
#define AF_LOCAL_H_

#ifdef __cplusplus
extern "C" {
#endif

int socketpair_af_local(int type, int proto, int sv[2]);

int shutdown_af_local(int fd, int how);

#ifdef __cplusplus
}
#endif

#endif /* AF_LOCAL_H_ */
