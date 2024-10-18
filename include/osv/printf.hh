/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PRINTF_HH_
#define PRINTF_HH_

#include <string>

namespace osv {

std::string sprintf(const char* fmt...);
std::string vsprintf(const char* fmt, va_list ap);

} // namespace osv

#endif /* PRINTF_HH_ */
