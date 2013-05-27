#include <sys/utsname.h>
#include <string.h>

struct utsname utsname = {
	.sysname	= "Linux",	/* lie, to avoid confusing the payload. */
	.nodename	= "osv.local",
	.release	= "3.7",
	.version	= "#1 SMP",
	.machine	= "x86_64",
};

int uname(struct utsname *uts)
{
	memcpy(uts, &utsname, sizeof(struct utsname));
	return 0;
}
