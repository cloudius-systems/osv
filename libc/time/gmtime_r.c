#include <time.h>

#include "__time.h"

struct tm *gmtime_r(const time_t *restrict t, struct tm *restrict result)
{
	__time_to_tm(*t, result);
	result->tm_isdst = 0;
	return result;
}
