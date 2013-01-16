#include <ctype.h>
#undef ispunct

int ispunct(int c)
{
	return isgraph(c) && !isalnum(c);
}
