/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_EXECVE_H
#define INCLUDED_OSV_EXECVE_H

#ifdef __cplusplus
extern "C" {
#endif

int osv_execve(const char *path, char *const argv[], char *const envp[], long* thread_id, int notification_fd);
long osv_waittid(long tid, int *status, int options);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDED_OSV_EXECVE_H */
