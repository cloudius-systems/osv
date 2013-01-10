#ifndef BARRIER_HH_
#define BARRIER_HH_

static inline void barrier()
{
    asm volatile("" : : : "memory");
}


#endif /* BARRIER_HH_ */
