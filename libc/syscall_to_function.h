#include <bits/syscall.h>
#include <unistd.h>

#define __OSV_TO_FUNCTION_SYS_open(filename, flags, perm) (open(filename, flags, perm))

#define __OSV_TO_FUNCTION_SYS_close(fd) (close(fd))

#define __OSV_TO_FUNCTION_SYS_lseek(file, off, whence) (lseek(file, off, whence))

#define __OSV_TO_FUNCTION_SYS_fcntl(fd, cmd, ...) (fcntl(fd, cmd __VA_OPT__(,) __VA_ARGS__))

#define __OSV_TO_FUNCTION_SYS_clock_gettime(c, t, x) (clock_gettime(c, t))

#define __OSV_TO_FUNCTION_SYS_access(p, i) (access(p, i))

#define __OSV_TO_FUNCTION_SYS_ioctl(fd, cmd, args) (ioctl(fd, cmd, args))

#define __OSV_TO_FUNCTION_SYS_unlink(path) (unlink(path))

#define __OSV_TO_FUNCTION_SYS_rmdir(path) (rmdir(path))

#define __OSV_TO_FUNCTION_SYS_readv(fd, cmd, args) (readv(fd, cmd, args))

#define __OSV_TO_FUNCTION_SYS_writev(fd, cmd, args) (writev(fd, cmd, args))

#undef __syscall
#define __syscall(sys_number, ...) (__OSV_TO_FUNCTION_##sys_number(__VA_ARGS__))
#undef syscall
#define syscall(sys_number, ...) (__OSV_TO_FUNCTION_##sys_number(__VA_ARGS__))
#undef syscall_cp
#define syscall_cp(sys_number, ...) (__OSV_TO_FUNCTION_##sys_number(__VA_ARGS__))
