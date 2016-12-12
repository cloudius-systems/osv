#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <stdbool.h>

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
   printf("Starting ttyname_r/ttyname test\n");
   // Basic flow for test
   // 1) Take fds for stdin, stdout and stderr and call ttyname_r and ttyname
   // 2) Use an invalid fd (-1) and call ttyname_r and ttyname

   char buf[256];
   memset(buf, 0, sizeof(buf));
   int fds[4] = {-1, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};

   for (int i = 0; i < 4; i++) {
      int retval = ttyname_r(fds[i], buf, sizeof(buf));
      printf("fd: %d ttyname_r retval: %d, buf: %s\n", fds[i], retval, buf);
      if (fds[i] < 0) {
         report("[ttyname_r] bad fds\0", retval == EBADF);
      } else {
         report("[ttyname_r] std* fd\0",
                retval == 0 && strcmp(buf, "/dev/console\0") == 0);
      }
      char* tty = ttyname(fds[i]);
      printf("fd: %d ttyname retval: %s\n", fds[i], tty);
      if (fds[i] < 0) {
         report("[ttyname] bad fds\0", tty == NULL);
      } else {
         report("[ttyname] std* fds\0", strcmp(buf, "/dev/console\0") == 0);
      }
      memset(buf, 0, sizeof(buf));
   }

   printf("SUMMARY: %u tests / %u failures\n", tests_total, tests_failed);
   return 0;
}
