#include <signal.h>
#include <string.h>

#if (SIGHUP == 1) && (SIGINT == 2) && (SIGQUIT == 3) && (SIGILL == 4) \
 && (SIGTRAP == 5) && (SIGABRT == 6) && (SIGBUS == 7) && (SIGFPE == 8) \
 && (SIGKILL == 9) && (SIGUSR1 == 10) && (SIGSEGV == 11) && (SIGUSR2 == 12) \
 && (SIGPIPE == 13) && (SIGALRM == 14) && (SIGTERM == 15) && (SIGSTKFLT == 16) \
 && (SIGCHLD == 17) && (SIGCONT == 18) && (SIGSTOP == 19) && (SIGTSTP == 20) \
 && (SIGTTIN == 21) && (SIGTTOU == 22) && (SIGURG == 23) && (SIGXCPU == 24) \
 && (SIGXFSZ == 25) && (SIGVTALRM == 26) && (SIGPROF == 27) && (SIGWINCH == 28) \
 && (SIGPOLL == 29) && (SIGPWR == 30) && (SIGSYS == 31)

#define sigmap(x) x

#else

static const char map[] = {
	[SIGHUP]    = 1,
	[SIGINT]    = 2,
	[SIGQUIT]   = 3,
	[SIGILL]    = 4,
	[SIGTRAP]   = 5,
	[SIGABRT]   = 6,
	[SIGBUS]    = 7,
	[SIGFPE]    = 8,
	[SIGKILL]   = 9,
	[SIGUSR1]   = 10,
	[SIGSEGV]   = 11,
	[SIGUSR2]   = 12,
	[SIGPIPE]   = 13,
	[SIGALRM]   = 14,
	[SIGTERM]   = 15,
	[SIGSTKFLT] = 16,
	[SIGCHLD]   = 17,
	[SIGCONT]   = 18,
	[SIGSTOP]   = 19,
	[SIGTSTP]   = 20,
	[SIGTTIN]   = 21,
	[SIGTTOU]   = 22,
	[SIGURG]    = 23,
	[SIGXCPU]   = 24,
	[SIGXFSZ]   = 25,
	[SIGVTALRM] = 26,
	[SIGPROF]   = 27,
	[SIGWINCH]  = 28,
	[SIGPOLL]   = 29,
	[SIGPWR]    = 30,
	[SIGSYS]    = 31
};

#define sigmap(x) ((unsigned)(x) > sizeof map ? 0 : map[(unsigned)(x)])

#endif

static const char strings[] =
	"Unknown signal\0"
	"Hangup\0"
	"Interrupt\0"
	"Quit\0"
	"Illegal instruction\0"
	"Trace/breakpoint trap\0"
	"Aborted\0"
	"Bus error\0"
	"Floating point exception\0"
	"Killed\0"
	"User defined signal 1\0"
	"Segmentation fault\0"
	"User defined signal 2\0"
	"Broken pipe\0"
	"Alarm clock\0"
	"Terminated\0"
	"Stack fault\0"
	"Child exited\0"
	"Continued\0"
	"Stopped (signal)\0"
	"Stopped\0"
	"Stopped (tty input)\0"
	"Stopped (tty output)\0"
	"Urgent I/O condition\0"
	"CPU time limit exceeded\0"
	"File size limit exceeded\0"
	"Virtual timer expired\0"
	"Profiling timer expired\0"
	"Window changed\0"
	"I/O possible\0"
	"Power failure\0"
	"Bad system call";

char *strsignal(int signum)
{
	char *s = (char *)strings;

	signum = sigmap(signum);
	if ((unsigned)signum - 1 > 31) signum = 0;

	for (; signum--; s++) for (; *s; s++);

	return s;
}
