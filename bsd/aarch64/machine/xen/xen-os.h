/*
 * Based on x64/machine/xen/xen-os.h and arm/os.h from Xen Mini OS
 *
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _XEN_OS_H_
#define _XEN_OS_H_
__BEGIN_DECLS

#if !defined(__XEN_INTERFACE_VERSION__)
/*
 * Can update to a more recent version when we implement
 * the hypercall page
 */
#define  __XEN_INTERFACE_VERSION__ 0x00030204
#endif

#include <xen/interface/xen.h>

extern shared_info_t *HYPERVISOR_shared_info;

static inline void rep_nop(void)
{
    __asm__ __volatile__ ( "yield" : : : "memory" );
}
#define cpu_relax() rep_nop()

#ifndef __ASSEMBLY__
#define likely(x)  __builtin_expect((x),1)
#define unlikely(x)  __builtin_expect((x),0)

#ifndef mb
#define mb() __asm__ __volatile__("dsb sy":::"memory")
#endif
#ifndef rmb
#define rmb() __asm__ __volatile__("dsb ld":::"memory");
#endif
#ifndef wmb
#define wmb() __asm__ __volatile__("dsb st":::"memory");
#endif
#ifdef SMP
#define smp_mb() mb()
#define smp_rmb() rmb()
#define smp_wmb() wmb()
#define smp_read_barrier_depends()      read_barrier_depends()
#define set_mb(var, value) do { xchg(&var, value); } while (0)
#else
#define smp_mb()        barrier()
#define smp_rmb()       barrier()
#define smp_wmb()       barrier()
#define smp_read_barrier_depends()      do { } while(0)
#define set_mb(var, value) do { var = value; barrier(); } while (0)
#endif

/* This is a barrier for the compiler only, NOT the processor! */
#define barrier() __asm__ __volatile__("": : :"memory")
/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic only.
 * use synch_test_and_clear_bit() if you need barrier.
 */
static __inline int test_and_clear_bit(int nr, volatile void * addr)
{
    uint8_t *byte = ((uint8_t *)addr) + (nr >> 3);
    uint8_t bit = 1 << (nr & 7);
    uint8_t orig;

    orig = __atomic_fetch_and(byte, ~bit, __ATOMIC_RELAXED);

    return (orig & bit) != 0;
}

/**
 * Atomically set a bit and return the old value.
 * Similar to test_and_clear_bit.
 */
static __inline__ int test_and_set_bit(int nr, volatile void *base)
{
    uint8_t *byte = ((uint8_t *)base) + (nr >> 3);
    uint8_t bit = 1 << (nr & 7);
    uint8_t orig;

    orig = __atomic_fetch_or(byte, bit, __ATOMIC_RELAXED);

    return (orig & bit) != 0;
}


/**
 * Test whether a bit is set. */
static __inline__ int test_bit(int nr, const volatile void *addr)
{
    const uint8_t *ptr = (const uint8_t *) addr;
    return ((1 << (nr & 7)) & (ptr[nr >> 3])) != 0;
}

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic only.
 * Use synch_set_bit() if you need a barrier.
 */
static __inline__ void set_bit(int nr, volatile void * addr)
{
    test_and_set_bit(nr, addr);
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic only.
 * Use synch_clear_bit() if you need a barrier.
 */
static __inline__ void clear_bit(int nr, volatile void * addr)
{
    test_and_clear_bit(nr, addr);
}

#endif /* !__ASSEMBLY__ */
__END_DECLS
#endif /* _XEN_OS_H_ */
