#ifndef SMP_HH_
#define SMP_HH_

#include "sched.hh"

void smp_launch();
sched::cpu* smp_initial_find_current_cpu();
void crash_other_processors();

#endif /* SMP_HH_ */
