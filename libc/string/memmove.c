#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <osv/string.h>

void *memmove(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = src;

	if (d==s) return d;
	if (s+n <= d || d+n <= s) return memcpy(d, s, n);

	if (d<s) {
        return memcpy(d, s, n);
	} else {
        return memcpy_backwards(d, s, n);
	}
}
