#include <errno.h>
#include <unistd.h>
#include <osv/pid.h>

// OSv's one process leads its own process group (see setsid()).
pid_t getpgid(pid_t pid)
{
    if (pid != 0 && pid != OSV_PID) {
        errno = ESRCH;
        return -1;
    }
    return OSV_PID;
}
