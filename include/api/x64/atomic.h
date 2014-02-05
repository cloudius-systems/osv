#ifndef _INTERNAL_ATOMIC_H
#define _INTERNAL_ATOMIC_H

#include <stdint.h>

static inline int a_ctz_64(uint64_t x)
{
	long r;
	__asm__( "bsf %1,%0" : "=r"(r) : "r"(x) );
	return r;
}

static inline int a_ctz_l(unsigned long x)
{
	long r;
	__asm__( "bsf %1,%0" : "=r"(r) : "r"(x) );
	return r;
}

static inline void a_and_64(volatile uint64_t *p, uint64_t v)
{
	__asm__( "lock ; andq %1, %0"
			 : "=m"(*(long *)p) : "r"(v) : "memory" );
}

static inline void a_or_64(volatile uint64_t *p, uint64_t v)
{
	__asm__( "lock ; orq %1, %0"
			 : "=m"(*(long *)p) : "r"(v) : "memory" );
}

static inline void a_store_l(volatile void *p, long x)
{
	__asm__( "movq %1, %0" : "=m"(*(long *)p) : "r"(x) : "memory" );
}

static inline void a_or_l(volatile void *p, long v)
{
	__asm__( "lock ; orq %1, %0"
		: "=m"(*(long *)p) : "r"(v) : "memory" );
}

static inline void *a_cas_p(volatile void *p, void *t, void *s)
{
	__asm__( "lock ; cmpxchg %3, %1"
		: "=a"(t), "=m"(*(long *)p) : "a"(t), "r"(s) : "memory" );
	return t;
}

static inline long a_cas_l(volatile void *p, long t, long s)
{
	__asm__( "lock ; cmpxchg %3, %1"
		: "=a"(t), "=m"(*(long *)p) : "a"(t), "r"(s) : "memory" );
	return t;
}

static inline int a_cas(volatile int *p, int t, int s)
{
	__asm__( "lock ; cmpxchgl %3, %1"
		: "=a"(t), "=m"(*p) : "a"(t), "r"(s) : "memory" );
	return t;
}

static inline void *a_swap_p(void *volatile *x, void *v)
{
	__asm__( "xchg %0, %1" : "=r"(v), "=m"(*(void **)x) : "0"(v) : "memory" );
	return v;
}
static inline long a_swap_l(volatile void *x, long v)
{
	__asm__( "xchg %0, %1" : "=r"(v), "=m"(*(long *)x) : "0"(v) : "memory" );
	return v;
}

static inline void a_or(volatile void *p, int v)
{
	__asm__( "lock ; orl %1, %0"
		: "=m"(*(int *)p) : "r"(v) : "memory" );
}

static inline void a_and(volatile void *p, int v)
{
	__asm__( "lock ; andl %1, %0"
		: "=m"(*(int *)p) : "r"(v) : "memory" );
}

static inline int a_swap(volatile int *x, int v)
{
	__asm__( "xchg %0, %1" : "=r"(v), "=m"(*x) : "0"(v) : "memory" );
	return v;
}

#define a_xchg a_swap

static inline int a_fetch_add(volatile int *x, int v)
{
	__asm__( "lock ; xadd %0, %1" : "=r"(v), "=m"(*x) : "0"(v) : "memory" );
	return v;
}

static inline void a_inc(volatile int *x)
{
	__asm__( "lock ; incl %0" : "=m"(*x) : "m"(*x) : "memory" );
}

static inline void a_dec(volatile int *x)
{
	__asm__( "lock ; decl %0" : "=m"(*x) : "m"(*x) : "memory" );
}

static inline void a_store(volatile int *p, int x)
{
	__asm__( "movl %1, %0" : "=m"(*p) : "r"(x) : "memory" );
}

static inline void a_spin()
{
	__asm__ __volatile__( "pause" : : : "memory" );
}

static inline void a_crash()
{
	__asm__ __volatile__( "hlt" : : : "memory" );
}


#endif
