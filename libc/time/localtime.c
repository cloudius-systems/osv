#include <time.h>

#include "__time.h"

struct tm *localtime(const time_t *t)
{
	static struct tm tm;
	__tzset();
	__time_to_tm(*t - __timezone, &tm);
	tm.tm_isdst = -1;
	return __dst_adjust(&tm);
}
