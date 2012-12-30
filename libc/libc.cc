#include "libc.hh"

int libc_error(int err)
{
    errno = err;
    return -1;
}
