#include <string.h>

char *__strchrnul(const char *, int);

#undef strchr
char *strchr(const char *s, int c)
{
	char *r = __strchrnul(s, c);
	return *(unsigned char *)r == (unsigned char)c ? r : 0;
}
