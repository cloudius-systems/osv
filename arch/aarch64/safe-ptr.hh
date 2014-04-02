/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SAFE_PTR_HH_
#define SAFE_PTR_HH_

#include <osv/compiler.h>

/* warning: not "safe" at all for now. */

template <typename T>
static inline bool
safe_load(const T* potentially_bad_pointer, T& data)
{
    data = *potentially_bad_pointer;
    return true;
}

template <typename T>
static inline bool
safe_store(const T* potentially_bad_pointer, const T& data)
{
    *potentially_bad_pointer = data;
    return true;
}

#endif /* SAFE_PTR_HH_ */
