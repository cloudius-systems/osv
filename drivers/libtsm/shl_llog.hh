/*
 * SHL - Library Log/Debug Interface
 *
 * Copyright (c) 2010-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * Library Log/Debug Interface
 * Libraries should always avoid producing side-effects. This includes writing
 * log-messages of any kind. However, you often don't want to disable debugging
 * entirely, therefore, the core objects often contain a pointer to a function
 * which performs logging. If that pointer is NULL (default), logging is
 * disabled.
 *
 * This header should never be installed into the system! This is _no_ public
 * header. Instead, copy it into your application if you want and use it there.
 * Your public library API should include something like this:
 *
 *   typedef void (*MYPREFIX_log_t) (void *data,
 *                                   const char *file,
 *                                   int line,
 *                                   const char *func,
 *                                   const char *subs,
 *                                   unsigned int sev,
 *                                   const char *format,
 *                                   va_list args);
 *
 * And then the user can supply such a function when creating a new context
 * object of your library or simply supply NULL. Internally, you have a field of
 * type "MYPREFIX_log_t llog" in your main structure. If you pass this to the
 * convenience helpers like llog_dbg(), llog_warn() etc. it will automatically
 * use the "llog" field to print the message. If it is NULL, nothing is done.
 *
 * The arguments of the log-function are defined as:
 *   data: User-supplied data field that is passed straight through.
 *   file: Zero terminated string of the file-name where the log-message
 *         occurred. Can be NULL.
 *   line: Line number of @file where the message occurred. Set to 0 or smaller
 *         if not available.
 *   func: Function name where the log-message occurred. Can be NULL.
 *   subs: Subsystem where the message occurred (zero terminated). Can be NULL.
 *   sev: Severity of log-message. An integer between 0 and 7 as defined below.
 *        These are identical to the linux-kernel severities so there is no need
 *        to include these in your public API. Every app can define them
 *        themselves, if they need it.
 *   format: Format string. Must not be NULL.
 *   args: Argument array
 *
 * The user should also be able to optionally provide a data field which is
 * always passed unmodified as first parameter to the log-function. This allows
 * to add context to the logger.
 */

#ifndef SHL_LLOG_H
#define SHL_LLOG_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

enum llog_severity {
	LLOG_FATAL = 0,
	LLOG_ALERT = 1,
	LLOG_CRITICAL = 2,
	LLOG_ERROR = 3,
	LLOG_WARNING = 4,
	LLOG_NOTICE = 5,
	LLOG_INFO = 6,
	LLOG_DEBUG = 7,
	LLOG_SEV_NUM,
};

typedef void (*llog_submit_t) (void *data,
			       const char *file,
			       int line,
			       const char *func,
			       const char *subs,
			       unsigned int sev,
			       const char *format,
			       va_list args);

static inline __attribute__((format(printf, 8, 9)))
void llog_format(llog_submit_t llog,
		 void *data,
		 const char *file,
		 int line,
		 const char *func,
		 const char *subs,
		 unsigned int sev,
		 const char *format,
		 ...)
{
	int saved_errno = errno;
	va_list list;

	if (llog) {
		va_start(list, format);
		errno = saved_errno;
		llog(data, file, line, func, subs, sev, format, list);
		va_end(list);
	}
}

#ifndef LLOG_SUBSYSTEM
static const char *LLOG_SUBSYSTEM __attribute__((__unused__));
#endif

#define LLOG_DEFAULT __FILE__, __LINE__, __func__, LLOG_SUBSYSTEM

#define llog_printf(obj, sev, format, ...) \
	llog_format((obj)->llog, \
		    (obj)->llog_data, \
		    LLOG_DEFAULT, \
		    (sev), \
		    (format), \
		    ##__VA_ARGS__)
#define llog_dprintf(obj, data, sev, format, ...) \
	llog_format((obj), \
		    (data), \
		    LLOG_DEFAULT, \
		    (sev), \
		    (format), \
		    ##__VA_ARGS__)

static inline __attribute__((format(printf, 4, 5)))
void llog_dummyf(llog_submit_t llog, void *data, unsigned int sev,
		 const char *format, ...)
{
}

/*
 * Helpers
 * They pick up all the default values and submit the message to the
 * llog-subsystem. The llog_debug() function will discard the message unless
 * BUILD_ENABLE_DEBUG is defined.
 */

#ifdef BUILD_ENABLE_DEBUG
	#define llog_ddebug(obj, data, format, ...) \
		llog_dprintf((obj), (data), LLOG_DEBUG, (format), ##__VA_ARGS__)
	#define llog_debug(obj, format, ...) \
		llog_ddebug((obj)->llog, (obj)->llog_data, (format), ##__VA_ARGS__)
#else
	#define llog_ddebug(obj, data, format, ...) \
		llog_dummyf((obj), (data), LLOG_DEBUG, (format), ##__VA_ARGS__)
	#define llog_debug(obj, format, ...) \
		llog_ddebug((obj)->llog, (obj)->llog_data, (format), ##__VA_ARGS__)
#endif

#define llog_info(obj, format, ...) \
	llog_printf((obj), LLOG_INFO, (format), ##__VA_ARGS__)
#define llog_dinfo(obj, data, format, ...) \
	llog_dprintf((obj), (data), LLOG_INFO, (format), ##__VA_ARGS__)
#define llog_notice(obj, format, ...) \
	llog_printf((obj), LLOG_NOTICE, (format), ##__VA_ARGS__)
#define llog_dnotice(obj, data, format, ...) \
	llog_dprintf((obj), (data), LLOG_NOTICE, (format), ##__VA_ARGS__)
#define llog_warning(obj, format, ...) \
	llog_printf((obj), LLOG_WARNING, (format), ##__VA_ARGS__)
#define llog_dwarning(obj, data, format, ...) \
	llog_dprintf((obj), (data), LLOG_WARNING, (format), ##__VA_ARGS__)
#define llog_error(obj, format, ...) \
	llog_printf((obj), LLOG_ERROR, (format), ##__VA_ARGS__)
#define llog_derror(obj, data, format, ...) \
	llog_dprintf((obj), (data), LLOG_ERROR, (format), ##__VA_ARGS__)
#define llog_critical(obj, format, ...) \
	llog_printf((obj), LLOG_CRITICAL, (format), ##__VA_ARGS__)
#define llog_dcritical(obj, data, format, ...) \
	llog_dprintf((obj), (data), LLOG_CRITICAL, (format), ##__VA_ARGS__)
#define llog_alert(obj, format, ...) \
	llog_printf((obj), LLOG_ALERT, (format), ##__VA_ARGS__)
#define llog_dalert(obj, data, format, ...) \
	llog_dprintf((obj), (data), LLOG_ALERT, (format), ##__VA_ARGS__)
#define llog_fatal(obj, format, ...) \
	llog_printf((obj), LLOG_FATAL, (format), ##__VA_ARGS__)
#define llog_dfatal(obj, data, format, ...) \
	llog_dprintf((obj), (data), LLOG_FATAL, (format), ##__VA_ARGS__)

/*
 * Default log messages
 * These macros can be used to produce default log messages. You can use them
 * directly in an "return" statement. The "v" variants automatically cast the
 * result to void so it can be used in return statements inside of void
 * functions. The "d" variants use the logging object directly as the parent
 * might not exist, yet.
 *
 * Most of the messages work only if debugging is enabled. This is, because they
 * are used in debug paths and would slow down normal applications.
 */

#define llog_dEINVAL(obj, data) \
	(llog_derror((obj), (data), "invalid arguments"), -EINVAL)
#define llog_EINVAL(obj) \
	(llog_dEINVAL((obj)->llog, (obj)->llog_data))
#define llog_vEINVAL(obj) \
	((void)llog_EINVAL(obj))
#define llog_vdEINVAL(obj, data) \
	((void)llog_dEINVAL((obj), (data)))

#define llog_dEFAULT(obj, data) \
	(llog_derror((obj), (data), "internal operation failed"), -EFAULT)
#define llog_EFAULT(obj) \
	(llog_dEFAULT((obj)->llog, (obj)->llog_data))
#define llog_vEFAULT(obj) \
	((void)llog_EFAULT(obj))
#define llog_vdEFAULT(obj, data) \
	((void)llog_dEFAULT((obj), (data)))

#define llog_dENOMEM(obj, data) \
	(llog_derror((obj), (data), "out of memory"), -ENOMEM)
#define llog_ENOMEM(obj) \
	(llog_dENOMEM((obj)->llog, (obj)->llog_data))
#define llog_vENOMEM(obj) \
	((void)llog_ENOMEM(obj))
#define llog_vdENOMEM(obj, data) \
	((void)llog_dENOMEM((obj), (data)))

#define llog_dEPIPE(obj, data) \
	(llog_derror((obj), (data), "fd closed unexpectedly"), -EPIPE)
#define llog_EPIPE(obj) \
	(llog_dEPIPE((obj)->llog, (obj)->llog_data))
#define llog_vEPIPE(obj) \
	((void)llog_EPIPE(obj))
#define llog_vdEPIPE(obj, data) \
	((void)llog_dEPIPE((obj), (data)))

#define llog_dERRNO(obj, data) \
	(llog_derror((obj), (data), "syscall failed (%d): %m", errno), -errno)
#define llog_ERRNO(obj) \
	(llog_dERRNO((obj)->llog, (obj)->llog_data))
#define llog_vERRNO(obj) \
	((void)llog_ERRNO(obj))
#define llog_vdERRNO(obj, data) \
	((void)llog_dERRNO((obj), (data)))

#define llog_dERR(obj, data, _r) \
	(errno = -(_r), llog_derror((obj), (data), "syscall failed (%d): %m", (_r)), (_r))
#define llog_ERR(obj, _r) \
	(llog_dERR((obj)->llog, (obj)->llog_data, (_r)))
#define llog_vERR(obj, _r) \
	((void)llog_ERR((obj), (_r)))
#define llog_vdERR(obj, data, _r) \
	((void)llog_dERR((obj), (data), (_r)))

#endif /* SHL_LLOG_H */
