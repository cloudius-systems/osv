/*
 * Copyright (C) 2021 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef EXPORT_H
#define EXPORT_H

//
// Please note that the macros below are used in the source files and
// are intended to expose the annotated symbols as public when the kernel
// files are compiled with the flag '-fvisibility=hidden'. When the kernel
// is compiled without visibility flag, these macro do not have any affect
// as all symbols in this case are exposed as public. So either way, the symbols
// annotated with these macros yield desired effect.
// We do not really need to define a macro for each Linux glibc library and we could
// have had single OSV_GLIBC_API macro instead of eight ones below. However by
// having a macro for each shared library file where a symbol is part of,
// we automatically self-document the code and in future could auto-generate some
// docs.
// More specifically, as an example, if given symbol is annotated with OSV_LIBC_API, it means
// that it physically part of the libc.so.6 file on Linux, etc.
#define OSV_LIBAIO_API __attribute__((__visibility__("default")))
#define OSV_LIBC_API __attribute__((__visibility__("default")))
#define OSV_LIBM_API __attribute__((__visibility__("default")))
#define OSV_LIBPTHREAD_API __attribute__((__visibility__("default")))
#define OSV_LIBUTIL_API __attribute__((__visibility__("default")))
#define OSV_LIBXENSTORE_API __attribute__((__visibility__("default")))
#define OSV_LD_LINUX_x86_64_API __attribute__((__visibility__("default")))

// This is to expose some symbols in libsolaris.so
#define OSV_LIB_SOLARIS_API __attribute__((__visibility__("default")))
//
// This is to expose some OSv functions intended to be used by modules
#define OSV_MODULE_API __attribute__((__visibility__("default")))

// In some very few cases, when source files are compiled without visibility
// flag in order to expose most symbols in the corresponding file, there are some specific
// symbols in the same file that we want to hide and this is where we use this macro.
// Regardless if we hide most symbols in the kernel or not, the annotated symbols would
// be always hidden.
#define OSV_HIDDEN __attribute__((__visibility__("hidden")))

#endif /* EXPORT_H */
