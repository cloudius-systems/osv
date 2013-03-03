#ifndef SMP_HH_
#define SMP_HH_

#include "sched.hh"

void smp_init();
void smp_launch();
sched::cpu* smp_initial_find_current_cpu();

#endif /* SMP_HH_ */
