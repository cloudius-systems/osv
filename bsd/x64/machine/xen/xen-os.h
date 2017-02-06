/******************************************************************************
 * os.h
 * 
 * random collection of macros and definition
 */

#ifndef _XEN_OS_H_
#define _XEN_OS_H_
__BEGIN_DECLS
#ifdef PAE
#define CONFIG_X86_PAE
#endif

#if !defined(__XEN_INTERFACE_VERSION__)  
/*  
 * Can update to a more recent version when we implement  
 * the hypercall page  
 */  
#define  __XEN_INTERFACE_VERSION__ 0x00030204  
#endif  

#include <xen/interface/xen.h>

/* Force a proper event-channel callback from Xen. */
void force_evtchn_callback(void);

extern int gdtset;

extern shared_info_t *HYPERVISOR_shared_info;

/* REP NOP (PAUSE) is a good thing to insert into busy-wait loops. */
static inline void rep_nop(void)
{
    __asm__ __volatile__ ( "rep;nop" : : : "memory" );
}
#define cpu_relax() rep_nop()

/* crude memory allocator for memory allocation early in 
 *  boot
 */
void *bootmem_alloc(unsigned int size);
void bootmem_free(void *ptr, unsigned int size);


/* Everything below this point is not included by assembler (.S) files. */
#ifndef __ASSEMBLY__

void printk(const char *fmt, ...);

/* some function prototypes */
void trap_init(void);

#define likely(x)  __builtin_expect((x),1)
#define unlikely(x)  __builtin_expect((x),0)

#ifndef XENHVM

/*
 * STI/CLI equivalents. These basically set and clear the virtual
 * event_enable flag in teh shared_info structure. Note that when
 * the enable bit is set, there may be pending events to be handled.
 * We may therefore call into do_hypervisor_callback() directly.
 */

#define __cli()                                                         \
do {                                                                    \
        vcpu_info_t *_vcpu;                                             \
        _vcpu = &HYPERVISOR_shared_info->vcpu_info[PCPU_GET(cpuid)];	\
        _vcpu->evtchn_upcall_mask = 1;                                  \
        barrier();                                                      \
} while (0)

#define __sti()                                                         \
do {                                                                    \
        vcpu_info_t *_vcpu;                                             \
        barrier();                                                      \
        _vcpu = &HYPERVISOR_shared_info->vcpu_info[PCPU_GET(cpuid)];	\
        _vcpu->evtchn_upcall_mask = 0;                                  \
        barrier(); /* unmask then check (avoid races) */                \
        if ( unlikely(_vcpu->evtchn_upcall_pending) )                   \
                force_evtchn_callback();                                \
} while (0)

#define __restore_flags(x)                                              \
do {                                                                    \
        vcpu_info_t *_vcpu;                                             \
        barrier();                                                      \
        _vcpu = &HYPERVISOR_shared_info->vcpu_info[PCPU_GET(cpuid)];	\
        if ((_vcpu->evtchn_upcall_mask = (x)) == 0) {                   \
                barrier(); /* unmask then check (avoid races) */        \
                if ( unlikely(_vcpu->evtchn_upcall_pending) )           \
                        force_evtchn_callback();                        \
        } 								\
} while (0)

/*
 * Add critical_{enter, exit}?
 *
 */
#define __save_and_cli(x)                                               \
do {                                                                    \
        vcpu_info_t *_vcpu;                                             \
        _vcpu = &HYPERVISOR_shared_info->vcpu_info[PCPU_GET(cpuid)];	\
        (x) = _vcpu->evtchn_upcall_mask;                                \
        _vcpu->evtchn_upcall_mask = 1;                                  \
        barrier();                                                      \
} while (0)


#define cli() __cli()
#define sti() __sti()
#define save_flags(x) __save_flags(x)
#define restore_flags(x) __restore_flags(x)
#define save_and_cli(x) __save_and_cli(x)

#define local_irq_save(x)       __save_and_cli(x)
#define local_irq_restore(x)    __restore_flags(x)
#define local_irq_disable()     __cli()
#define local_irq_enable()      __sti()

#define mtx_lock_irqsave(lock, x) {local_irq_save((x)); mtx_lock_spin((lock));}
#define mtx_unlock_irqrestore(lock, x) {mtx_unlock_spin((lock)); local_irq_restore((x)); }
#define spin_lock_irqsave mtx_lock_irqsave
#define spin_unlock_irqrestore mtx_unlock_irqrestore

#else
#endif

#ifndef mb
#define mb() __asm__ __volatile__("mfence":::"memory")
#endif
#ifndef rmb
#define rmb() __asm__ __volatile__("lfence":::"memory");
#endif
#ifndef wmb
#define wmb() barrier()
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

#define LOCK_PREFIX ""
#define LOCK ""
#define ADDR (*(volatile long *) addr)
/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
typedef struct { volatile int counter; } atomic_t;

static inline unsigned int __ffs(unsigned int word)
{
        __asm__("bsfl %1,%0"
                :"=r" (word)
                :"rm" (word));
        return word;
}

#define xen_xchg(ptr,v) \
        ((__typeof__(*(ptr)))__xchg((unsigned long)(v),(ptr),sizeof(*(ptr))))
struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))
static __inline unsigned long __xchg(unsigned long x, volatile void * ptr,
                                   int size)
{
    switch (size) {
    case 1:
        __asm__ __volatile__("xchgb %b0,%1"
                             :"=q" (x)
                             :"m" (*__xg(ptr)), "0" (x)
                             :"memory");
        break;
    case 2:
        __asm__ __volatile__("xchgw %w0,%1"
                             :"=r" (x)
                             :"m" (*__xg(ptr)), "0" (x)
                             :"memory");
        break;
    case 4:
        __asm__ __volatile__("xchgl %0,%1"
                             :"=r" (*(unsigned int *)x)
                             :"m" (*__xg(ptr)), "0" (*(unsigned int *)x)
                             :"memory");
        break;
    case 8:
        __asm__ __volatile__("xchgq %0,%1"
                             :"=r" (x)
                             :"m" (*__xg(ptr)), "0" (x)
                             :"memory");
        break;

    }
    return x;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */
static __inline int test_and_clear_bit(int nr, volatile void * addr)
{
        int oldbit;

        __asm__ __volatile__( LOCK_PREFIX
                "btrl %2,%1\n\tsbbl %0,%0"
                :"=r" (oldbit),"=m" (ADDR)
                :"Ir" (nr) : "memory");
        return oldbit;
}

static __inline int constant_test_bit(int nr, const volatile void * addr)
{
    return ((1UL << (nr & 31)) & (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}

static __inline int variable_test_bit(int nr, volatile void * addr)
{
    int oldbit;
    
    __asm__ __volatile__(
        "btl %2,%1\n\tsbbl %0,%0"
        :"=r" (oldbit)
        :"m" (ADDR),"Ir" (nr));
    return oldbit;
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 constant_test_bit((nr),(addr)) : \
 variable_test_bit((nr),(addr)))


/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __inline__ void set_bit(int nr, volatile void * addr)
{
        __asm__ __volatile__( LOCK_PREFIX
                "btsl %1,%0"
                :"=m" (ADDR)
                :"Ir" (nr));
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_clear_bit() and/or smp_mb__after_clear_bit()
 * in order to ensure changes are visible on other processors.
 */
static __inline__ void clear_bit(int nr, volatile void * addr)
{
        __asm__ __volatile__( LOCK_PREFIX
                "btrl %1,%0"
                :"=m" (ADDR)
                :"Ir" (nr));
}

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 * 
 * Atomically increments @v by 1.  Note that the guaranteed
 * useful range of an atomic_t is only 24 bits.
 */ 
static __inline__ void atomic_inc(atomic_t *v)
{
        __asm__ __volatile__(
                LOCK "incl %0"
                :"=m" (v->counter)
                :"m" (v->counter));
}


#define rdtscll(val) \
     __asm__ __volatile__("rdtsc" : "=A" (val))

#endif /* !__ASSEMBLY__ */
__END_DECLS
#endif /* _OS_H_ */
