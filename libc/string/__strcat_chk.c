#include <string.h>
#include <assert.h>

char *__strcat_chk(char *d, const char *s, size_t s1len)
{
    // TODO: This repeats some of strncat's work. Make it more efficent.
    size_t n = strlen(s);
    size_t space = s1len;
    for (const char *tmp = d; *tmp; tmp++) {
        --space;
        assert (space >= 0);
    }
    // Need space for strlen(s) and the null
    assert (space > n);
    return strncat(d, s, n);
}
