#include "libc.hh"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <boost/algorithm/string/split.hpp>
#include <type_traits>
#include <limits>
#include <sys/resource.h>
#include <pwd.h>
#include <sys/utsname.h>

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

char* realpath(const char* path, char* resolved_path)
{
    // assumes cwd == /
    std::vector<std::string> components;
    std::string spath = path;
    boost::split(components, spath, [] (char c) { return c == '/'; });
    std::vector<std::string> tmp;
    for (auto c : components) {
        if (c == "" || c == ".") {
            continue;
        } else if (c == "..") {
            if (!tmp.empty()) {
                tmp.pop_back();
            }
        } else {
            tmp.push_back(c);
        }
    }
    std::string ret;
    for (auto c : tmp) {
        ret += "/" + c;
    }
    ret = ret.substr(0, PATH_MAX - 1);
    if (!resolved_path) {
        resolved_path = static_cast<char*>(malloc(ret.size() + 1));
    }
    strcpy(resolved_path, ret.c_str());
    return resolved_path;
}

char* getenv(const char* name)
{
    // no environment
    return NULL;
}

int putenv(char* string)
{
    return 0; // no environent
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
    default:
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
    strcpy(u->nodename, "home");
    strcpy(u->release, "3.7");
    strcpy(u->version, "#1 SMP");
    strcpy(u->machine, "x86_64");
    return 0;
}
