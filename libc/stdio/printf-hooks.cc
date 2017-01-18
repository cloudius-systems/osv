/*
 * Copyright (C) 2016 ScyllaDB Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Since 2009 (see blog post http://udrepper.livejournal.com/20948.html),
// glibc has hooks for extending printf() to support additional types,
// format specifiers and modifiers.
//
// We do not support these hooks in OSv yet (doing so would require heavy
// modifications to Musl's printf implementation), but just offer here a
// stub, do-nothing implementation which returns -1 for every function
// (telling the caller that the attempt was not successful).
// This implementation is only enough in case a library wants to add
// additional types to printf (e.g., libquadmath does this to support
// 128-bit quadruple-precision floating point types), but the application
// does not really try to print out such values.

#include <printf.h>

#include <osv/stubbing.hh>

int register_printf_specifier(int, printf_function, printf_arginfo_size_function)
{
    WARN_STUBBED();
    return -1;
}
int register_printf_modifier(const wchar_t *) {
    WARN_STUBBED();
    return -1;
}
int register_printf_type(printf_va_arg_function) {
    WARN_STUBBED();
    return -1;
}
