#include <errno.h>
#include <sys/wait.h>
#include <osv/kernel_config_fork.h>

#if CONF_fork
#include <osv/fork.hh>

// waitpid()/wait4() are backed by the fork() emulation's child registry
// (libc/process/fork.cc).  A child created by fork() records its exit status
// there when it exits; here the parent reaps it.

extern "C"
pid_t waitpid(pid_t pid, int *status, int options)
{
    return osv::fork::wait_child(pid, status, options);
}

extern "C"
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{
    if (rusage) {
        __builtin_memset(rusage, 0, sizeof(*rusage));
    }
    return osv::fork::wait_child(pid, status, options);
}

extern "C"
pid_t wait(int *status)
{
    return osv::fork::wait_child(-1, status, 0);
}

#else // !CONF_fork

// Without fork() there are never any children to wait for.
extern "C"
pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)pid; (void)status; (void)options;
    errno = ECHILD;
    return -1;
}

#endif // CONF_fork
