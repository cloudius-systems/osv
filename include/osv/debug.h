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

__BEGIN_DECLS
void debug(const char *msg);
void debug_write(const char *msg, size_t len);

int vkprintf(const char *__restrict fmt, va_list ap)
	__attribute__((format(printf, 1, 0)));
int kprintf(const char *__restrict fmt, ...)
	__attribute__((format(printf, 1, 2)));

void tprintf(const char* tag, logger_severity severity, const char* _fmt, ...);
__END_DECLS

#endif /* _DEBUG_H */
