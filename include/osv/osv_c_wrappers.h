/*
 * Copyright (C) 2016 XLAB, d.o.o.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_C_WRAPPERS_H
#define INCLUDED_OSV_C_WRAPPERS_H

#include <limits.h>
#include <sys/types.h>
#include <osv/mount.h>

#ifdef __cplusplus
extern "C" {
#endif

// This C enum should be kept in sync with the C++ enum class 
// sched::thread::status defined in sched.hh
enum osv_thread_status {
  invalid,
  prestarted,
  unstarted,
  waiting,
  sending_lock,
  running,
  queued,
  waking,
  terminating,
  terminated
};

struct osv_thread {
  // 32-bit thread id
  long id;

  // CPU the thread is running on
  long cpu_id;

  // Total CPU time used by the thread (in milliseconds)
  long cpu_ms;

  // Number of times this thread was context-switched in
  long switches;

  // Number of times this thread was migrated between CPUs
  long migrations;

  // Number of times this thread was preempted (still runnable, but switched out)
  long preemptions;

  float priority;
  long stack_size;

  enum osv_thread_status status;

  // Thread name
  char* name;
};

/*
Save in *tid_arr array TIDs of all threads from app which "owns" input tid/thread.
*tid_arr is allocated with malloc, *len holds length.
Caller is responsible to free tid_arr.
Returns 0 on success, error code on error.
*/
int osv_get_all_app_threads(pid_t tid, pid_t** tid_arr, size_t* len);

/*
Save in *thread_arr array info about all threads.
*thread_arr is allocated with malloc, *len holds length.
Caller is responsible to free thread_arr and thread names
in osv_thread struct.
Returns 0 on success, error code on error.
*/
int osv_get_all_threads(osv_thread** thread_arr, size_t *len);

/*
 * Return OSv version as C string. The returned C string is
 * allocated with malloc and caller is responsible to free it
 * if non null.
 */
char *osv_version();

/*
 * Return OSv command line C string. The returned C string is
 * allocated with malloc and caller is responsible to free it
 * if non null.
 */
char *osv_cmdline();

/*
 * Return hypervisor name as C string. The returned C string is
 * allocated with malloc and caller is responsible to free it
 * if non null.
 */
char *osv_hypervisor_name();

/*
 * Return firmware vendor as C string. The returned C string is
 * allocated with malloc and caller is responsible to free it
 * if non null.
 */
char *osv_firmware_vendor();

/*
 * Return processor features as C string. The returned C string is
 * allocated with malloc and caller is responsible to free it
 * if non null.
 */
char *osv_processor_features();

/*
 * Return pointer to OSv debug buffer.
 */
const char *osv_debug_buffer();

/*
 * Return true if OSv debug flag (--verbose) is enabled, otherwise return false.
 */
bool osv_debug_enabled();

/*
 * Pass a function pointer of a routine which will be invoked
 * upon termination of the current app. Useful for resources cleanup.
 */
void osv_current_app_on_termination_request(void (*handler)());

#ifdef __cplusplus
}
#endif

#endif /* INCLUDED_OSV_C_WRAPPERS_H */
