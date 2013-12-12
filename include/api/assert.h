#ifndef _API_ASSERT_H
#define _API_ASSERT_H

#include <features.h>

#undef assert

/* We want the assert() not to be compiled out in the kernel */
#if defined NDEBUG && !defined _KERNEL
#define	assert(x) (void)0
#else
#define assert(x) ((void)((x) || (__assert_fail(#x, __FILE__, __LINE__, __func__),0)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

void __assert_fail (const char *, const char *, int, const char *) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
#endif /* _API_ASSERT_H */
