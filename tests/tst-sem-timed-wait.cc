#include <osv/semaphore.hh>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

unsigned int tests_total = 0, tests_failed = 0;

void report(const char* name, bool passed)
{
   static const char* status[] = {"FAIL", "PASS"};
   printf("%s: %s\n", status[passed], name);
   tests_total += 1;
   tests_failed += !passed;
}

int main(void)
{
   printf("Starting sem_timed_wait test\n");

   // Basic flow for test
   // 1) Create a semaphore (initialized to 0)
   // 2) Do a timed-wait on it
   // 3) We're never signaled/woken so our stack-allocated wait_record remains
   // on the semaphore's waiters list
   //
   // In the failure case the end result is a stacktrace that looks like:
   // Assertion failed: !hook.is_linked()
   //(/usr/include/boost/intrusive/detail/generic_hook.hpp: destructor_impl: 47)

   //[backtrace]
   //0x0000000000225a48 <__assert_fail+24>
   //0x00000000003c40e9 <???+3948777>
   //0x00000000003c4242 <semaphore::wait(unsigned int, sched::timer*)+98>
   //0x0000100000c01057 <???+12587095>

   semaphore sem(0);
   sched::timer tmr(*sched::thread::current());
   osv::clock::wall::duration time((std::chrono::seconds(0)));
   tmr.set(time);
   bool ret_val = sem.wait(1, &tmr);
   report("sem_timedwait\0", ret_val == false);
   printf("SUMMARY: %u tests / %u failures\n", tests_total, tests_failed);
   return tests_failed == 0 ? 0 : 1;
}
