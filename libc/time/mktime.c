#include <time.h>

#include "__time.h"

time_t mktime(struct tm *tm)
{
	int isdst = tm->tm_isdst;
	time_t t, lt;

	__tzset();

	tm->tm_sec += __timezone;
	if (isdst > 0) tm->tm_sec += __dst_offset;

	t = __tm_to_time(tm);
	
	lt = t - __timezone;
	if (isdst > 0) lt -= __dst_offset;
	__time_to_tm(lt, tm);

	__dst_adjust(tm);
	
	return t;
}
