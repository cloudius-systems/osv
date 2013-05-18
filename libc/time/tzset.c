#include <time.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "libc.h"

#include "__time.h"

long  __timezone = 0;
int   __daylight = 0;
char *__tzname[2] = { 0, 0 };
int   __dst_offset = 0;

weak_alias(__timezone, timezone);
weak_alias(__daylight, daylight);
weak_alias(__tzname, tzname);

static char std_name[TZNAME_MAX+1];
static char dst_name[TZNAME_MAX+1];

/* all elements are zero-based */
static struct rule {
	signed char month;
	signed char week;
	short day;
	int time;
} __dst_start, __dst_end;

static void zname(char *d, char **s)
{
	int i;
	for (i=0; i<TZNAME_MAX && isalpha(d[i]=**s); i++, (*s)++);
	d[i] = 0;
}

static int hhmmss(char **s)
{
	int ofs = strtol(*s, s, 10)*3600;
	if (ofs >= 0) {
		if (**s == ':') ofs += strtol(*s+1, s, 10)*60;
		if (**s == ':') ofs += strtol(*s+1, s, 10);
	} else {
		if (**s == ':') ofs -= strtol(*s+1, s, 10)*60;
		if (**s == ':') ofs -= strtol(*s+1, s, 10);
	}
	return ofs;
}

static int dstrule(struct rule *rule, char **s)
{
	if (**s != ',') return -1;
	switch (*++*s) {
	case 'J':
		rule->month = 'J';
		rule->day = strtol(*s+1, s, 10)-1;
		break;
	case 'M':
		rule->month = strtol(*s+1, s, 10)-1;
		if (**s != '.' || rule->month < 0 || rule->month > 11)
			return -1;
		rule->week = strtol(*s+1, s, 10)-1;
		if (**s != '.' || rule->week < 0 || rule->week > 4)
			return -1;
		rule->day = strtol(*s+1, s, 10);
		if (rule->day < 0 || rule->day > 6)
			return -1;
		break;
	default:
		rule->month = 'L';
		rule->day = strtol(*s+1, s, 10);
		break;
	}
	if (**s == '/') {
		(*s)++;
		rule->time = hhmmss(s);
	} else rule->time = 7200;
	return 0;
}

void tzset(void)
{
	char *z, *a;
	
	strcpy(std_name, "GMT");
	strcpy(dst_name, "GMT");
	__tzname[0] = std_name;
	__tzname[1] = dst_name;
	__timezone = 0;
	__daylight = 0;
	
	if (!(z = getenv("TZ")) || !isalpha(*z)) return;

	zname(std_name, &z);
	__timezone = hhmmss(&z);

	zname(dst_name, &z);
	if (dst_name[0]) __daylight=1;
	a = z;
	__dst_offset = hhmmss(&z) - __timezone;
	if (z==a) __dst_offset = -3600;

	if (dstrule(&__dst_start, &z) || dstrule(&__dst_end, &z))
		__daylight = 0;
}

void __tzset(void)
{
	static mutex_t lock;
	static int init;
	if (init) return;
	LOCK(lock);
	if (!init) tzset();
	init=1;
	UNLOCK(lock);
}

static int is_leap(int year)
{
	year -= 100;
	return !(year&3) && ((year%100) || !(year%400));
}

static int cutoff_yday(struct tm *tm, struct rule *rule)
{
	static const char days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	static const int first_day[] = {0,31,59,90,120,151,181,212,243,273,304,335};
	int yday, mday, leap;
	
	switch (rule->month) {
	case 'J':
		return rule->day + (tm->tm_mon > 1 && is_leap(tm->tm_year));
	case 'L':
		return rule->day;
	default:
		yday = first_day[rule->month];
		leap = is_leap(tm->tm_year);
		if (rule->month > 1 && leap) yday++;
		mday = (rule->day - (yday + tm->tm_wday - tm->tm_yday) + 1400)%7 + 7*rule->week;
		if (mday >= days_in_month[rule->month] + (leap && rule->month == 1))
			mday -= 7;
		return mday + yday;
	}
}

struct tm *__dst_adjust(struct tm *tm)
{
	time_t t;
	int start, end, secs;
	int after_start, before_end;

	if (tm->tm_isdst >= 0) return tm;
	if (!__daylight) {
		tm->tm_isdst = 0;
		return tm;
	}
	
	secs = tm->tm_hour*3600 + tm->tm_min*60 + tm->tm_sec;
	start = cutoff_yday(tm, &__dst_start);
	end = cutoff_yday(tm, &__dst_end);

	after_start = (tm->tm_yday > start || (tm->tm_yday == start && secs >= __dst_start.time));
	before_end = (tm->tm_yday < end || (tm->tm_yday == end && secs < __dst_end.time));

	if ((after_start && before_end) || ((end < start) && (after_start || before_end))) {
		tm->tm_sec -= __dst_offset;
		tm->tm_isdst = 1;
		t = __tm_to_time(tm);
		return __time_to_tm(t, tm);
	} else tm->tm_isdst = 0;

	return tm;
}
