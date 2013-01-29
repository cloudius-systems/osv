#include <string.h>

#undef strpbrk
char *strpbrk(const char *s, const char *b)
{
	s += strcspn(s, b);
	return *s ? (char *)s : 0;
}
