#define _GNU_SOURCE
#include <time.h>

#include "__time.h"

time_t timegm(struct tm *tm)
{
	return __tm_to_time(tm);
}
