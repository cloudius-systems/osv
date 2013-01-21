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

template <typename N>
N strtoN(const char* s, char** e, int base)
{
    typedef typename std::make_unsigned<N>::type tmp_type;
    tmp_type tmp = 0;
    auto max = std::numeric_limits<N>::max();
    auto min = std::numeric_limits<N>::min();

    while (*s && isspace(*s)) {
        ++s;
    }
    int sign = 1;
    if (*s == '+' || *s == '-') {
        sign = *s++ == '-' ? -1 : 1;
    }
    auto overflow = sign > 0 ? max : min;
    if (base == 0 && s[0] == '0' && s[1] == 'x') {
        base = 16;
    } else if (base == 0 && s[0] == '0') {
        base = 8;
    }
    if (base == 16 && s[0] == '0' && s[1] == 'x') {
        s += 2;
    }
    if (base == 0) {
        base = 10;
    }

    auto convdigit = [=] (char c, int& digit) {
        if (isdigit(c)) {
            digit = c - '0';
        } else if (isalpha(c)) {
            digit = tolower(c) - 'a' + 10;
        } else {
            return false;
        }
        return digit < base;
    };

    int digit;
    while (*s && convdigit(*s, digit)) {
        auto old = tmp;
        tmp = tmp * base + digit;
        if (tmp < old) {
            errno = ERANGE;
            return overflow;
        }
        ++s;
    }
    if ((sign < 0 && tmp > tmp_type(max)) || (tmp > tmp_type(-min))) {
        errno = ERANGE;
        return overflow;
    }
    if (sign < 0) {
        tmp = -tmp;
    }
    if (e) {
        *e = const_cast<char*>(s);
    }
    return tmp;
}

long strtol(const char* s, char** e, int base)
{
    return strtoN<long>(s, e, base);
}

unsigned long strtoul(const char* s, char** e, int base)
{
    return strtoN<unsigned long>(s, e, base);
}

long long strtoll(const char* s, char** e, int base)
{
    return strtoN<long long>(s, e, base);
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

int sched_yield()
{
    sched::thread::yield();
    return 0;
}
