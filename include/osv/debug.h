/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _DEBUG_H
#define _DEBUG_H 1

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdarg.h>

typedef enum logger_severity_e {
     logger_debug = 0,
     logger_info = 1,
     logger_warn = 2,
     logger_error = 3,
     // Suppress output, even errors
     logger_none = 4
 } logger_severity;

#if CONF_logger_debug
    #define tprintf_d(tag, ...) tprintf((tag), logger_debug, __VA_ARGS__)
#else
    #define tprintf_d(tag, ...) do{}while(0)
#endif

#define tprintf_i(tag, ...) tprintf((tag), logger_info, __VA_ARGS__)
#define tprintf_w(tag, ...) tprintf((tag), logger_warn, __VA_ARGS__)
#define tprintf_e(tag, ...) tprintf((tag), logger_error, __VA_ARGS__)

__BEGIN_DECLS
void debug(const char *msg);
void debug_write(const char *msg, size_t len);

/* a lockless version that doesn't take any locks before printing,
   should be used only to debug faults */
void debug_ll(const char *fmt, ...);

int vkprintf(const char *__restrict fmt, va_list ap)
	__attribute__((format(printf, 1, 0)));
int kprintf(const char *__restrict fmt, ...)
	__attribute__((format(printf, 1, 2)));

void tprintf(const char* tag, logger_severity severity, const char* _fmt, ...);
__END_DECLS

#endif /* _DEBUG_H */
