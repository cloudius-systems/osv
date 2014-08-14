#include "stdio_impl.h"
#include <libc.h>

size_t __fread_chk (void *restrict destv, size_t dest_size, size_t size, size_t nmemb, FILE *restrict f)
{
   if (size > dest_size) {
        _chk_fail(__FUNCTION__);
   }
   return fread(destv, size, nmemb, f);
}
