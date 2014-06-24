#include <sys/utsname.h>
#include <string.h>
#include <errno.h>

extern struct utsname utsname;

int sethostname(const char *name, size_t len)
{
    if (len < 0 || len > sizeof utsname.nodename) {
        errno = EINVAL;
        return -1;
    }
    strncpy(utsname.nodename, name, len);
    if (len < sizeof utsname.nodename) {
        utsname.nodename[len] = 0;
    }
    return 0;
}
