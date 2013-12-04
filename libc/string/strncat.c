#include <string.h>
#include <assert.h>

char *strncat(char *__restrict d, const char *__restrict s, size_t n)
{
	char *a = d;
	d += strlen(d);
	while (n && *s) n--, *d++ = *s++;
	*d++ = 0;
	return a;
}

/* Used by code compiled on Linux with -D_FORTIFY_SOURCE */
char *__strncat_chk (char *d, const char *s, size_t n, size_t s1len)
{
    // TODO: This repeats some of strncat's work. Make it more efficent.
    size_t space = s1len;
    for (const char *tmp = d; *tmp; tmp++) {
        --space;
        assert (space >= 0);
    }
    // Need space for strlen(s) or n (whichever is smaller) and the null
    assert (space > strnlen(s, n));
    return strncat(d, s, n);
}
