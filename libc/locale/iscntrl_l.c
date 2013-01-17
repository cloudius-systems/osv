#include <ctype.h>

#undef iscntrl_l
int iscntrl_l(int c, locale_t l)
{
	return iscntrl(c);
}
