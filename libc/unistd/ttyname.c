#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <memory.h>

char* ttyname(int fd)
{
   static char buf[TTY_NAME_MAX];
   memset(buf, 0, sizeof(buf));
   int result;
   if ((result = ttyname_r(fd, buf, sizeof(buf)))) {
      errno = result;
      return NULL;
   }
   return buf;
}
