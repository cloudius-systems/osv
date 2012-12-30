#ifndef LIBC_HH_
#define LIBC_HH_

#include <errno.h>

int libc_error(int err);

template <typename T>
T* libc_error_ptr(int err);

template <typename T>
T* libc_error_ptr(int err)
{
    libc_error(err);
    return nullptr;
}

#endif /* LIBC_HH_ */
