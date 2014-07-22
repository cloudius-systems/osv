/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_EXCEPTION_UTILS_HH
#define _OSV_EXCEPTION_UTILS_HH

#include <exception>

static inline std::string what(std::exception_ptr e_ptr)
{
    try {
        std::rethrow_exception(e_ptr);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "";
    }
}

static inline std::string current_what()
{
    return what(std::current_exception());
}

#endif
