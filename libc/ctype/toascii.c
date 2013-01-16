#include <ctype.h>
#undef toascii

/* nonsense function that should NEVER be used! */
int toascii(int c)
{
	return c & 0x7f;
}
