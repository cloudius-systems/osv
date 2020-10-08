#include <bits/syscall.h>
#include <unistd.h>
#include <errno.h>

#define sys_open open

#define __OSV_TO_FUNCTION_SYS_open          open
#define __OSV_TO_FUNCTION_SYS_close         close
#define __OSV_TO_FUNCTION_SYS_lseek         lseek
#define __OSV_TO_FUNCTION_SYS_fcntl         fcntl
#define __OSV_TO_FUNCTION_SYS_clock_gettime clock_gettime
#define __OSV_TO_FUNCTION_SYS_access        access
#define __OSV_TO_FUNCTION_SYS_ioctl         ioctl
#define __OSV_TO_FUNCTION_SYS_fstat         fstat
#define __OSV_TO_FUNCTION_SYS_lstat         lstat
#define __OSV_TO_FUNCTION_SYS_unlink        unlink
#define __OSV_TO_FUNCTION_SYS_rmdir         rmdir
#define __OSV_TO_FUNCTION_SYS_readv         readv
#define __OSV_TO_FUNCTION_SYS_writev        writev

#undef __syscall
#define __syscall(sys_number, ...) (__OSV_TO_FUNCTION_##sys_number(__VA_ARGS__) < 0 ? -errno : 0)
#undef syscall
#define syscall(sys_number, ...) (__OSV_TO_FUNCTION_##sys_number(__VA_ARGS__))
#undef syscall_cp
#define syscall_cp(sys_number, ...) (__OSV_TO_FUNCTION_##sys_number(__VA_ARGS__))
