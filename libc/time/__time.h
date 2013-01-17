time_t __tm_to_time(struct tm *);
struct tm *__time_to_tm(time_t, struct tm *);
void __tzset(void);
struct tm *__dst_adjust(struct tm *tm);

extern long __timezone;
extern int __daylight;
extern int __dst_offset;
extern char *__tzname[2];
