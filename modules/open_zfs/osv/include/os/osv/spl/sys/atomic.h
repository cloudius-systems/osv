// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL atomic operations - standalone header.
 * Provides all atomic operations that OpenZFS 2.3.6 needs,
 * using GCC __atomic builtins for the OSv platform.
 */
#ifndef _SPL_OSV_ATOMIC_H
#define	_SPL_OSV_ATOMIC_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory barriers */
#define	membar_consumer()	__atomic_thread_fence(__ATOMIC_ACQUIRE)
#define	membar_producer()	__atomic_thread_fence(__ATOMIC_RELEASE)
#define	membar_sync()		__atomic_thread_fence(__ATOMIC_SEQ_CST)

/* 64-bit atomics */
static inline void
atomic_add_64(volatile uint64_t *target, int64_t delta)
{
	__atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline uint64_t
atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
{
	return (__atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST));
}

static inline void
atomic_sub_64(volatile uint64_t *target, int64_t delta)
{
	__atomic_sub_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline void
atomic_inc_64(volatile uint64_t *target)
{
	__atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}

static inline uint64_t
atomic_inc_64_nv(volatile uint64_t *target)
{
	return (__atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST));
}

static inline void
atomic_dec_64(volatile uint64_t *target)
{
	__atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}

static inline uint64_t
atomic_dec_64_nv(volatile uint64_t *target)
{
	return (__atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST));
}

static inline uint64_t
atomic_cas_64(volatile uint64_t *target, uint64_t cmp, uint64_t newval)
{
	uint64_t expected = cmp;
	__atomic_compare_exchange_n(target, &expected, newval, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return (expected);
}

static inline uint64_t
atomic_swap_64(volatile uint64_t *target, uint64_t newval)
{
	return (__atomic_exchange_n(target, newval, __ATOMIC_SEQ_CST));
}

static inline uint64_t
atomic_load_64(volatile uint64_t *target)
{
	return (__atomic_load_n(target, __ATOMIC_RELAXED));
}

static inline void
atomic_store_64(volatile uint64_t *target, uint64_t val)
{
	__atomic_store_n(target, val, __ATOMIC_RELAXED);
}

/* 32-bit atomics */
static inline void
atomic_add_32(volatile uint32_t *target, int32_t delta)
{
	__atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST);
}

static inline uint32_t
atomic_add_32_nv(volatile uint32_t *target, int32_t delta)
{
	return (__atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST));
}

static inline uint_t
atomic_add_int_nv(volatile uint_t *target, int delta)
{
	return (__atomic_add_fetch(target, delta, __ATOMIC_SEQ_CST));
}

static inline void
atomic_inc_32(volatile uint32_t *target)
{
	__atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t
atomic_inc_32_nv(volatile uint32_t *target)
{
	return (__atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST));
}

static inline void
atomic_dec_32(volatile uint32_t *target)
{
	__atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t
atomic_dec_32_nv(volatile uint32_t *target)
{
	return (__atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST));
}

static inline uint32_t
atomic_cas_32(volatile uint32_t *target, uint32_t cmp, uint32_t newval)
{
	uint32_t expected = cmp;
	__atomic_compare_exchange_n(target, &expected, newval, 0,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return (expected);
}

static inline uint32_t
atomic_swap_32(volatile uint32_t *target, uint32_t newval)
{
	return (__atomic_exchange_n(target, newval, __ATOMIC_SEQ_CST));
}

static inline void
atomic_or_32(volatile uint32_t *target, uint32_t value)
{
	__atomic_or_fetch(target, value, __ATOMIC_SEQ_CST);
}

static inline void
atomic_and_32(volatile uint32_t *target, uint32_t value)
{
	__atomic_and_fetch(target, value, __ATOMIC_SEQ_CST);
}

/* 8-bit atomics */
static inline uint8_t
atomic_or_8_nv(volatile uint8_t *target, uint8_t value)
{
	return (__atomic_or_fetch(target, value, __ATOMIC_SEQ_CST));
}

static inline void
atomic_or_8(volatile uint8_t *target, uint8_t value)
{
	__atomic_or_fetch(target, value, __ATOMIC_SEQ_CST);
}

/* Pointer atomics */
static inline void *
atomic_cas_ptr(volatile void *target, void *cmp, void *newval)
{
	return ((void *)atomic_cas_64((volatile uint64_t *)target,
	    (uint64_t)cmp, (uint64_t)newval));
}

/*
 * Linux-style atomic_t operations.
 * Used by zvol and other OpenZFS code.
 */
#ifndef _ATOMIC_T_DEFINED
#define	_ATOMIC_T_DEFINED
typedef struct {
	volatile int counter;
} atomic_t;
#endif

static inline int
atomic_read(const atomic_t *v)
{
	return (__atomic_load_n(&v->counter, __ATOMIC_RELAXED));
}

static inline void
atomic_set(atomic_t *v, int i)
{
	__atomic_store_n(&v->counter, i, __ATOMIC_RELAXED);
}

static inline void
atomic_inc(atomic_t *v)
{
	__atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

static inline void
atomic_dec(atomic_t *v)
{
	__atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST);
}

static inline int
atomic_inc_return(atomic_t *v)
{
	return (__atomic_add_fetch(&v->counter, 1, __ATOMIC_SEQ_CST));
}

static inline int
atomic_dec_return(atomic_t *v)
{
	return (__atomic_sub_fetch(&v->counter, 1, __ATOMIC_SEQ_CST));
}

#ifdef __cplusplus
}
#endif

#endif	/* !_SPL_OSV_ATOMIC_H */
