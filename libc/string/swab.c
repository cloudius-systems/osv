#include <unistd.h>

void swab(const void *__restrict _src, void *__restrict _dest, ssize_t n)
{
	const char *src = _src;
	char *dest = _dest;
	for (; n>0; n-=2) {
		dest[0] = src[1];
		dest[1] = src[0];
		dest += 2;
		src += 2;
	}
}
