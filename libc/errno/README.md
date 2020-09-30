The only file - `strerror.c` provides deprecated `sys_errlist` and `sys_nerr` symbols
that musl does not implement. So should stay as is and should be unaffected by future musl upgrades.
