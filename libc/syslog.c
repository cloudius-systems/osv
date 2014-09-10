
// adapted from musl's version, just writes to stdio

#include <syslog.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <libc.h>

static mutex_t lock;
static char log_ident[32];
static int log_opt;
static int log_facility = LOG_USER;

void openlog(const char *ident, int opt, int facility)
{
    LOCK(lock);

    if (ident) {
        size_t n = strnlen(ident, sizeof log_ident - 1);
        memcpy(log_ident, ident, n);
        log_ident[n] = 0;
    } else {
        log_ident[0] = 0;
    }
    log_opt = opt;
    log_facility = facility;

    UNLOCK(lock);
}

void closelog(void)
{
}

void __syslog_chk(int priority, int flag, const char *message, ...)
{
    LOCK(lock);

    va_list ap;
    va_start(ap, message);

    char timebuf[16];
    time_t now;
    struct tm tm;
    char buf[256];
    int pid;
    int l, l2;

    if (!(priority & LOG_FACMASK)) priority |= log_facility;

    now = time(NULL);
    gmtime_r(&now, &tm);
    strftime(timebuf, sizeof timebuf, "%b %e %T", &tm);

    pid = (log_opt & LOG_PID) ? getpid() : 0;
    l = snprintf(buf, sizeof buf, "<%d>%s %s%s%.0d%s: ",
        priority, timebuf, log_ident, "["+!pid, pid, "]"+!pid);
    l2 = vsnprintf(buf+l, sizeof buf - l, message, ap);
    if (l2 >= 0) {
        if (l2 >= sizeof buf - l) l = sizeof buf - 1;
        else l += l2;
        if (buf[l-1] != '\n') buf[l++] = '\n';
        fwrite(buf, 1, l, LOG_PRI(priority) >= LOG_ERR ? stderr : stdout);            
    }

    va_end(ap);

    UNLOCK(lock);
}

