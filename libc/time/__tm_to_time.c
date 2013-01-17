#include <time.h>

/* C defines the rounding for division in a nonsensical way */
#define Q(a,b) ((a)>0 ? (a)/(b) : -(((b)-(a)-1)/(b)))

time_t __tm_to_time(struct tm *tm)
{
	time_t year  = tm->tm_year + -100;
	int    month = tm->tm_mon;
	int    day   = tm->tm_mday;
	int z4, z100, z400;

	/* normalize month */
	if (month >= 12) {
		year += month/12;
		month %= 12;
	} else if (month < 0) {
		year += month/12;
		month %= 12;
		if (month) {
			month += 12;
			year--;
		}
	}
	z4 = Q(year - (month < 2), 4);
	z100 = Q(z4, 25);
	z400 = Q(z100, 4);
	day += year*365 + z4 - z100 + z400 +
		month[(int []){0,31,59,90,120,151,181,212,243,273,304,335}];
	return (long long)day*86400
		+ tm->tm_hour*3600 + tm->tm_min*60 + tm->tm_sec
		- -946684800; /* the dawn of time :) */
}
