#include <stdio.h>
#include <stdlib.h>
#include <langinfo.h>
#include <time.h>
#include "__time.h"

// FIXME: integer overflows

const char *__langinfo(nl_item);

size_t strftime(char *restrict s, size_t n, const char *restrict f, const struct tm *restrict tm)
{
	nl_item item;
	int val;
	const char *fmt;
	size_t l;
	for (l=0; *f && l<n; f++) {
		if (*f == '%') {
do_fmt:
		switch (*++f) {
		case '%':
			goto literal;
		case 'E':
		case 'O':
			goto do_fmt;
		case 'a':
			item = ABDAY_1 + tm->tm_wday;
			goto nl_strcat;
		case 'A':
			item = DAY_1 + tm->tm_wday;
			goto nl_strcat;
		case 'h':
		case 'b':
			item = ABMON_1 + tm->tm_mon;
			goto nl_strcat;
		case 'B':
			item = MON_1 + tm->tm_mon;
			goto nl_strcat;
		case 'c':
			item = D_T_FMT;
			goto nl_strftime;
		case 'C':
			val = (1900+tm->tm_year) / 100;
			fmt = "%02d";
			goto number;
		case 'd':
			val = tm->tm_mday;
			fmt = "%02d";
			goto number;
		case 'D':
			fmt = "%m/%d/%y";
			goto recu_strftime;
		case 'e':
			val = tm->tm_mday;
			fmt = "%2d";
			goto number;
		case 'F':
			fmt = "%Y-%m-%d";
			goto recu_strftime;
		case 'g':
			// FIXME
			val = 0; //week_based_year(tm)%100;
			fmt = "%02d";
			goto number;
		case 'G':
			// FIXME
			val = 0; //week_based_year(tm);
			fmt = "%04d";
			goto number;
		case 'H':
			val = tm->tm_hour;
			fmt = "%02d";
			goto number;
		case 'I':
			val = tm->tm_hour;
			if (!val) val = 12;
			else if (val > 12) val -= 12;
			fmt = "%02d";
			goto number;
		case 'j':
			val = tm->tm_yday+1;
			fmt = "%03d";
			goto number;
		case 'm':
			val = tm->tm_mon+1;
			fmt = "%02d";
			goto number;
		case 'M':
			val = tm->tm_min;
			fmt = "%02d";
			goto number;
		case 'n':
			s[l++] = '\n';
			continue;
		case 'p':
			item = tm->tm_hour >= 12 ? PM_STR : AM_STR;
			goto nl_strcat;
		case 'r':
			item = T_FMT_AMPM;
			goto nl_strftime;
		case 'R':
			fmt = "%H:%M";
			goto recu_strftime;
		case 'S':
			val = tm->tm_sec;
			fmt = "%02d";
			goto number;
		case 't':
			s[l++] = '\t';
			continue;
		case 'T':
			fmt = "%H:%M:%S";
			goto recu_strftime;
		case 'u':
			val = tm->tm_wday ? tm->tm_wday : 7;
			fmt = "%d";
			goto number;
		case 'U':
		case 'V':
		case 'W':
			// FIXME: week number mess..
			continue;
		case 'w':
			val = tm->tm_wday;
			fmt = "%d";
			goto number;
		case 'x':
			item = D_FMT;
			goto nl_strftime;
		case 'X':
			item = T_FMT;
			goto nl_strftime;
		case 'y':
			val = tm->tm_year % 100;
			fmt = "%02d";
			goto number;
		case 'Y':
			val = tm->tm_year + 1900;
			fmt = "%04d";
			goto number;
		case 'z':
			if (tm->tm_isdst < 0) continue;
			val = -__timezone - (tm->tm_isdst ? __dst_offset : 0);
			l += snprintf(s+l, n-l, "%+.2d%.2d", val/3600, abs(val%3600)/60);
			continue;
		case 'Z':
			if (tm->tm_isdst < 0 || !__tzname[0] || !__tzname[0][0])
				continue;
			l += snprintf(s+l, n-l, "%s", __tzname[!!tm->tm_isdst]);
			continue;
		default:
			return 0;
		}
		}
literal:
		s[l++] = *f;
		continue;
number:
		l += snprintf(s+l, n-l, fmt, val);
		continue;
nl_strcat:
		l += snprintf(s+l, n-l, "%s", __langinfo(item));
		continue;
nl_strftime:
		fmt = __langinfo(item);
recu_strftime:
		l += strftime(s+l, n-l, fmt, tm);
	}
	if (l >= n) return 0;
	s[l] = 0;
	return l;
}
