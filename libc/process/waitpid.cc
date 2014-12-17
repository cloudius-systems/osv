#include <errno.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *status, int options)
{
    errno = ECHILD;
    return -1;
}
