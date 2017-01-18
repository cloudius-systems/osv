/*
 * Copyright (C) 2016 XLAB, d.o.o.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_C_WRAPPERS_H
#define INCLUDED_OSV_C_WRAPPERS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
Save in *tid_arr array TIDs of all threads from app which "owns" input tid/thread.
*tid_arr is allocated with malloc, *len holds lenght.
Caller is responsible to free tid_arr.
Returns 0 on success, error code on error.
*/
int osv_get_all_app_threads(pid_t tid, pid_t** tid_arr, size_t* len);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDED_OSV_C_WRAPPERS_H */
