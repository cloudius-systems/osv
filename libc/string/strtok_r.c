#include <string.h>
#include <libc.h>

#undef strtok_r
char *strtok_r(char *__restrict s, const char *__restrict sep, char **__restrict p)
{
	if (!s && !(s = *p)) return NULL;
	s += strspn(s, sep);
	if (!*s) return *p = 0;
	*p = s + strcspn(s, sep);
	if (**p) *(*p)++ = 0;
	else *p = 0;
	return s;
}

weak_alias(strtok_r, __strtok_r);
