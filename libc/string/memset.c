#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#define SS (sizeof(size_t))
#define ALIGN (sizeof(size_t)-1)
#define ONES ((size_t)-1/UCHAR_MAX)

void *memset_base(void *dest, int c, size_t n)
{
	unsigned char *s = (unsigned char *)dest;
	c = (unsigned char)c;
	for (; ((uintptr_t)s & ALIGN) && n; n--) *s++ = c;
	if (n) {
		size_t *w, k = ONES * c;
		for (w = (size_t *)s; n>=SS; n-=SS, w++) *w = k;
		for (s = (unsigned char *)w; n; n--, s++) *s = c;
	}
	return dest;
}
