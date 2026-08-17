// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL debug - comprehensive ASSERT/VERIFY macros for OpenZFS 2.3.6.
 *
 * Based on the FreeBSD SPL debug.h. Provides all the ASSERT/VERIFY
 * variants that OpenZFS expects.
 */
#ifndef _SPL_OSV_DEBUG_H
#define	_SPL_OSV_DEBUG_H

/*
 * Block the compat debug.h wrapper if it ever gets included.
 */
#define	_OPENSOLARIS_SYS_DEBUG_H_
/* Also block the old contrib debug.h */
#define	_SYS_DEBUG_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GCC/Clang attributes used by OpenZFS.
 */
#ifndef __maybe_unused
#define	__maybe_unused	__attribute__((unused))
#endif

#ifndef __must_check
#define	__must_check	__attribute__((__warn_unused_result__))
#endif

#ifndef __printflike
#define	__printflike(a, b)	__attribute__((__format__(__printf__, a, b)))
#endif

#ifndef expect
#define	expect(expr, value)	(__builtin_expect((expr), (value)))
#endif

#ifndef likely
#define	likely(x)	__builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define	unlikely(x)	__builtin_expect(!!(x), 0)
#endif

/*
 * spl_panic and spl_assert - core assertion infrastructure.
 */
#if defined(__COVERITY__) || defined(__clang_analyzer__)
__attribute__((__noreturn__))
#endif
extern void spl_panic(const char *file, const char *func, int line,
    const char *fmt, ...);
extern void spl_dumpstack(void);

static inline int
spl_assert(const char *buf, const char *file, const char *func, int line)
{
	spl_panic(file, func, line, "%s", buf);
	return (0);
}

/*
 * Also provide assfail/assfail3 for compatibility with old contrib debug.h.
 */
extern int assfail(const char *, const char *, int);
extern void assfail3(const char *, uintmax_t, const char *, uintmax_t,
    const char *, int);

#define	PANIC(fmt, a...)						\
	spl_panic(__FILE__, __FUNCTION__, __LINE__, fmt, ## a)

#define	VERIFY(cond)							\
	(void) (unlikely(!(cond)) &&					\
	    spl_assert("VERIFY(" #cond ") failed\n",			\
	    __FILE__, __FUNCTION__, __LINE__))

#define	VERIFYF(cond, str, ...)		do {				\
		if (unlikely(!(cond)))					\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY(" #cond ") failed " str "\n", __VA_ARGS__);\
	} while (0)

#define	VERIFY3B(LEFT, OP, RIGHT)	do {				\
		const boolean_t _verify3_left = (boolean_t)!!(LEFT);	\
		const boolean_t _verify3_right = (boolean_t)!!(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3B(" #LEFT ", "  #OP ", "  #RIGHT ") "	\
		    "failed (%d " #OP " %d)\n",				\
		    (int)_verify3_left, (int)_verify3_right);		\
	} while (0)

#define	VERIFY3S(LEFT, OP, RIGHT)	do {				\
		const int64_t _verify3_left = (int64_t)(LEFT);		\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3S(" #LEFT ", "  #OP ", "  #RIGHT ") "	\
		    "failed (%lld " #OP " %lld)\n",			\
		    (long long)_verify3_left,				\
		    (long long)_verify3_right);				\
	} while (0)

#define	VERIFY3U(LEFT, OP, RIGHT)	do {				\
		const uint64_t _verify3_left = (uint64_t)(LEFT);	\
		const uint64_t _verify3_right = (uint64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3U(" #LEFT ", "  #OP ", "  #RIGHT ") "	\
		    "failed (%llu " #OP " %llu)\n",			\
		    (unsigned long long)_verify3_left,			\
		    (unsigned long long)_verify3_right);		\
	} while (0)

#define	VERIFY3P(LEFT, OP, RIGHT)	do {				\
		const uintptr_t _verify3_left = (uintptr_t)(LEFT);	\
		const uintptr_t _verify3_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3P(" #LEFT ", "  #OP ", "  #RIGHT ") "	\
		    "failed (%p " #OP " %p)\n",				\
		    (void *)_verify3_left,				\
		    (void *)_verify3_right);				\
	} while (0)

#define	VERIFY0(RIGHT)	do {						\
		const int64_t _verify0_right = (int64_t)(RIGHT);	\
		if (unlikely(!(0 == _verify0_right)))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0(" #RIGHT ") failed (%lld)\n",		\
		    (long long)_verify0_right);				\
	} while (0)

#define	VERIFY0P(RIGHT)	do {						\
		const uintptr_t _verify0_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(0 == _verify0_right)))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0P(" #RIGHT ") failed (%p)\n",		\
		    (void *)_verify0_right);				\
	} while (0)

/* Formatted variants */
#define	VERIFY3BF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const boolean_t _verify3_left = (boolean_t)!!(LEFT);	\
		const boolean_t _verify3_right = (boolean_t)!!(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3B(" #LEFT ", " #OP ", "  #RIGHT ") "	\
		    "failed (%d " #OP " %d) " STR "\n",			\
		    (int)_verify3_left, (int)_verify3_right,		\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY3SF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const int64_t _verify3_left = (int64_t)(LEFT);		\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3S(" #LEFT ", " #OP ", "  #RIGHT ") "	\
		    "failed (%lld " #OP " %lld) " STR "\n",		\
		    (long long)_verify3_left, (long long)_verify3_right,\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY3UF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const uint64_t _verify3_left = (uint64_t)(LEFT);	\
		const uint64_t _verify3_right = (uint64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3U(" #LEFT ", " #OP ", "  #RIGHT ") "	\
		    "failed (%llu " #OP " %llu) " STR "\n",		\
		    (unsigned long long)_verify3_left,			\
		    (unsigned long long)_verify3_right,			\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY3PF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const uintptr_t _verify3_left = (uintptr_t)(LEFT);	\
		const uintptr_t _verify3_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3P(" #LEFT ", " #OP ", "  #RIGHT ") "	\
		    "failed (%p " #OP " %p) " STR "\n",			\
		    (void *)_verify3_left, (void *)_verify3_right,	\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY0PF(RIGHT, STR, ...)	do {				\
		const uintptr_t _verify3_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(0 == _verify3_right)))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0P(" #RIGHT ") failed (%p) " STR "\n",	\
		    (void *)_verify3_right,				\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY0F(RIGHT, STR, ...)	do {				\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(0 == _verify3_right)))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0(" #RIGHT ") failed (%lld) " STR "\n",	\
		    (long long)_verify3_right,				\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY_IMPLY(A, B) \
	((void)(likely((!(A)) || (B)) ||				\
	    spl_assert("(" #A ") implies (" #B ")",			\
	    __FILE__, __FUNCTION__, __LINE__)))

#define	VERIFY_EQUIV(A, B)	VERIFY3B(A, ==, B)

/*
 * Debugging disabled (default for OSv)
 */
#ifdef NDEBUG

#define	ASSERT(x)		((void)0)
#define	ASSERT3B(x, y, z)	((void)0)
#define	ASSERT3S(x, y, z)	((void)0)
#define	ASSERT3U(x, y, z)	((void)0)
#define	ASSERT3P(x, y, z)	((void)0)
#define	ASSERT0(x)		((void)0)
#define	ASSERT0P(x)		((void)0)
#define	ASSERT3BF(x, y, z, str, ...)	((void)0)
#define	ASSERT3SF(x, y, z, str, ...)	((void)0)
#define	ASSERT3UF(x, y, z, str, ...)	((void)0)
#define	ASSERT3PF(x, y, z, str, ...)	((void)0)
#define	ASSERT0PF(x, str, ...)		((void)0)
#define	ASSERT0F(x, str, ...)		((void)0)
#define	ASSERTF(x, str, ...)		((void)0)
#define	IMPLY(A, B)			((void)0)
#define	EQUIV(A, B)			((void)0)

#define	ASSERT64(x)
#define	ASSERT32(x)

/*
 * Debugging enabled
 */
#else

#define	ASSERT3B	VERIFY3B
#define	ASSERT3S	VERIFY3S
#define	ASSERT3U	VERIFY3U
#define	ASSERT3P	VERIFY3P
#define	ASSERT0		VERIFY0
#define	ASSERT0P	VERIFY0P
#define	ASSERT3BF	VERIFY3BF
#define	ASSERT3SF	VERIFY3SF
#define	ASSERT3UF	VERIFY3UF
#define	ASSERT3PF	VERIFY3PF
#define	ASSERT0PF	VERIFY0PF
#define	ASSERT0F	VERIFY0F
#define	ASSERTF		VERIFYF
#define	ASSERT		VERIFY
#define	IMPLY		VERIFY_IMPLY
#define	EQUIV		VERIFY_EQUIV

#if defined(_LP64)
#define	ASSERT64(x)	ASSERT(x)
#define	ASSERT32(x)
#else
#define	ASSERT64(x)
#define	ASSERT32(x)	ASSERT(x)
#endif

#endif /* NDEBUG */

/*
 * STATIC - conditionally static for debugging.
 */
#if defined(DEBUG) && !defined(__sun)
#define	STATIC
#else
#define	STATIC static
#endif

/*
 * NOTE() / _NOTE() macros.
 */
#ifndef _NOTE
#define	_NOTE(x)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_DEBUG_H */
