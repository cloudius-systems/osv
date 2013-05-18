#include "libc.hh"
#include "sched.hh"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <boost/algorithm/string/split.hpp>
#include <type_traits>
#include <limits>
#include <sys/resource.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <osv/debug.h>
#include <sched.h>

int libc_error(int err)
{
    errno = err;
    return -1;
}

#undef errno

int __thread errno;

int* __errno_location()
{
    return &errno;
}

int getrlimit(int resource, struct rlimit *rlim)
{
    auto set = [=] (rlim_t r) { rlim->rlim_cur = rlim->rlim_max = r; };
    switch (resource) {
    case RLIMIT_STACK:
        set(64*1024); // FIXME: something realer
        break;
    case RLIMIT_NOFILE:
        set(1024*10); // FIXME: larger?
        break;
    case RLIMIT_CORE:
        set(RLIM_INFINITY);
        break;
    case RLIMIT_NPROC:
        set(RLIM_INFINITY);
        break;
    case RLIMIT_AS:
        set(RLIM_INFINITY);
        break;
    default:
        kprintf("getrlimit: resource %d not supported\n", resource);
        abort();
    }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim)
{
    // osv - no limits
    return 0;
}

uid_t geteuid()
{
    return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result)
{
    auto alloc = [&](int n) { auto tmp = buf; buf += n; return tmp; };
    auto save = [&](const char* s) { return strcpy(alloc(strlen(s) + 1), s); };

    pwd->pw_name = save("osv");
    pwd->pw_passwd = save("*");
    pwd->pw_uid = 0;
    pwd->pw_gid = 0;
    pwd->pw_gecos = save("");
    pwd->pw_dir = save("");
    pwd->pw_shell = save("");
    *result = pwd;
    return 0;
}

struct passwd* getpwuid(uid_t uid)
{
    static struct passwd ret;
    static char buf[300];
    struct passwd *p;
    int e;

    e = getpwuid_r(uid, &ret, buf, sizeof(buf), &p);
    if (e == 0) {
        return &ret;
    } else {
        return libc_error_ptr<passwd>(e);
    }
}

int uname(struct utsname* u)
{
    // lie, to avoid confusing the payload.
    strcpy(u->sysname, "Linux");
    strcpy(u->nodename, "osv.local");
    strcpy(u->release, "3.7");
    strcpy(u->version, "#1 SMP");
    strcpy(u->machine, "x86_64");
    return 0;
}

int sched_yield()
{
    sched::thread::yield();
    return 0;
}

extern "C"
int getloadavg(double loadavg[], int nelem)
{
    int i;

    for (i = 0; i < nelem; i++)
        loadavg[i] = 0.5;

    return 0;
}

extern "C" int sysinfo(struct sysinfo *info)
{
    memset(info, 0, sizeof(struct sysinfo));
    return 0;
}

