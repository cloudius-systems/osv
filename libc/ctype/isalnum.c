#include <ctype.h>
#undef isalnum

int isalnum(int c)
{
	return isalpha(c) || isdigit(c);
}
