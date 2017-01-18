/*
 * Copyright (C) 2016 ScyllaDB Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This is a Linux-specific header files for non-standard printf() hooks.

#ifndef INCLUDED_PRINTF_H
#define INCLUDED_PRINTF_H

#define __NEED_size_t
#define __NEED_wchar_t
#define __NEED_va_list
#define __NEED_FILE
#include <bits/alltypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int printf_function(
        FILE *, const struct printf_info *, const void *const *);
typedef int printf_arginfo_size_function(
        const struct printf_info *, size_t, int *, int *);
typedef void printf_va_arg_function(void *, va_list *);

int register_printf_specifier(int, printf_function, printf_arginfo_size_function);
int register_printf_modifier(const wchar_t *);
int register_printf_type(printf_va_arg_function);

#ifdef __cplusplus
}
#endif

#endif
