
#include <sys/types.h>
#include <errno.h>

/*
 * Glibc provides two incompatible versions of strerror_r and uses
 * redirection magic for the XPG compliants ones in <string.h>,
 * so we must avoid including that header as long as we use the glibc
 * headers instead of the musl ones.
 */
extern char *strerror (int);
extern size_t strlen (const char *);
extern void *memcpy (void *__restrict, const void *__restrict, size_t);

int __xpg_strerror_r(int err, char *buf, size_t buflen)
{
	char *msg = strerror(err);
	size_t l = strlen(msg);
	if (l >= buflen) {
		if (buflen) {
			memcpy(buf, msg, buflen-1);
			buf[buflen-1] = 0;
		}
		return ERANGE;
	}
	memcpy(buf, msg, l+1);
	return 0;
}

char* strerror_r(int err, char *buf, size_t buflen)
{
	__xpg_strerror_r(err, buf, buflen);
	return buf;
}
