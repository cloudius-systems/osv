#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int ttyname_r(int fd, char *buf, size_t buflen)
{
   if (fd < 0) {
      return EBADF;
   }
   // OSv doesn't support any virtual terminals or ptys so return
   // the fixed pathname of /dev/console
   char* ttyname = "/dev/console\0";
   size_t len = strlen(ttyname);
   if (!isatty(fd)) {
      return ENOTTY;
   } else {
      // Size must be large enough to hold the pathname + NULL char. buf must
      // also not be NULL
      if (buflen < len + 1 || !buf) {
         return ERANGE;
      } else {
         memcpy(buf, ttyname, len);
         buf[len] = 0;
         return 0;
      }
   }
   return 0;
}
