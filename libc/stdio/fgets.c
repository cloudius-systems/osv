#include "stdio_impl.h"
#include <string.h>

#define MIN(a,b) ((a)<(b) ? (a) : (b))

char *fgets(char *restrict s, int n, FILE *restrict f)
{
	char *p = s;
	unsigned char *z;
	size_t k;
	int c;

	if (n--<=1) {
		if (n) return 0;
		*s = 0;
		return s;
	}

	FLOCK(f);

	while (n) {
		z = memchr(f->rpos, '\n', f->rend - f->rpos);
		k = z ? z - f->rpos + 1 : f->rend - f->rpos;
		k = MIN(k, n);
		memcpy(p, f->rpos, k);
		f->rpos += k;
		p += k;
		n -= k;
		if (z || !n) break;
		if ((c = getc_unlocked(f)) < 0) {
			if (p==s || !feof(f)) s = 0;
			break;
		}
		n--;
		if ((*p++ = c) == '\n') break;
	}
	*p = 0;

	FUNLOCK(f);

	return s;
}

weak_alias(fgets, fgets_unlocked);
