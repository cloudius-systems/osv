#include <stdlib.h>
#include <string.h>
#include "libc.h"

char *__strndup(const char *s, size_t n)
{
	return strndup(s, n);
}
