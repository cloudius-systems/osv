/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ERROR_H_
#define ERROR_H_

#include <errno.h>

struct error {
#ifdef __cplusplus
public:
    explicit error() = default;
    explicit error(int _errno) : _errno(_errno) {}
    error(const error& e) = default;
    bool bad() const { return _errno != 0; }
    int get() const { return _errno; }
    int to_libc() const;
private:
#endif
    int _errno = 0;
};

typedef struct error error;

static inline error no_error()
{
    return (error) { 0 };
}

static inline struct error make_error(int _errno)
{
    return (error) { _errno };
}

static inline bool error_bad(error e)
{
#ifdef __cplusplus
    return e.bad();
#else
    return e._errno != 0;
#endif
}

static inline int error_get(error e)
{
#ifdef __cplusplus
    return e.get();
#else
    return e._errno;
#endif
}

inline static int error_to_libc(error e)
{
    if (!error_bad(e)) {
        return 0;
    } else {
        errno = error_get(e);
        return -1;
    }
}

#ifdef __cplusplus

inline int error::to_libc() const
{
    return error_to_libc(*this);
}

#endif

#endif /* ERROR_H_ */
