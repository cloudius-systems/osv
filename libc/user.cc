// OSV does not support different users, and getuid(), getgid() and friends
// always return 0.
// In this file we implement various user-handling functions in traditional
// libc, in the most sensible way we can given our limitations. In particular:
// 1. Every user name resolves to user id 0.
// 2. setuid() to any other user id but 0 fails on assertion

#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#include <debug.hh>

static char username[] = "osv";
static char password[] = "";
static char gecos[] = "OSV User";
static char homedir[] = "/";
static char shell[] = "?";

static struct passwd single_user = {
        username, password, 0, 0, gecos, homedir, shell
};

struct passwd *getpwnam(const char *name)
{
   return &single_user;
}

int setuid(uid_t uid)
{
    assert(uid == 0);
    return 0;
}

int setgid(gid_t gid)
{
    assert(gid == 0);
    return 0;
}
