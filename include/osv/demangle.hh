/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DEMANGLE_HH_
#define DEMANGLE_HH_

#include <stddef.h>

#include <stddef.h>
#include <memory>

/**
 * OSv namespace
 */
namespace osv {

/** @name #include <osv/demangle.hh>
 * Functions for demangling of mangled C++ symbol names
 */

/**@{*/
/**
 * Demangle the given symbol name into a pre-allocated buffer.
 *
 * This function demangles a given C++ symbol name, placing the demangled name
 * into the given buffer. This function does not allocate memory, so it can be
 * used in contexts where allocations are not allowed.
 *
 * \return \c true if demangling was successful, and the result was placed in
 * buf. If "name" was not a mangled name, \c false is returned and buf remains
 * unchanged. This includes the case that "name" is a C symbol, not a C++
 * symbol.
 */
bool demangle(const char *name, char *buf, size_t len);

/**
 * Demangle the given symbol name into a newly allocated buffer.
 *
 * This function demangles a given C++ symbol name, placing the demangled name
 * into newly allocated buffer. The new buffer is returned wrapped in a
 * std::unique_ptr<char> so that it will be automatically freed when no longer
 * needed by the caller.
 *
 * \return a std::unique_ptr<char> owning a newly allocated C string if
 * demangling was successful, or empty when this was not a mangled name (this
 * includes the case that "name" is a C symbol, not a C++ symbol).
 */
std::unique_ptr<char> demangle(const char *name);

/**
 * A demangler tool for repeated demangling
 *
 * osv::demangle(const char *name) allocates a new buffer on each demangling
 * operation, which is inefficient if demangling needs to be done in a loop.
 * The demangler class allows reuse of the same buffer over and over, reducing
 * the number of allocations - but
 * unlike osv::demangle(const char *name, char *buf, size_t len) does not
 * eliminate them completely and grows the buffer as needed.
 * An example on how to use the demangler:
 * \code
 * osv::demangler demangle;
 * for (name : names) {
 *     char *n = demangle(name);
 *     if (n)
 *         printf("%s -> %s\n", name, n);
 *     else
 *         printf("%s\n", name);
 * }
 * \endcode
 */
struct demangler {
private:
    char *_buf = nullptr;
    size_t _len = 0;
public:
    ~demangler();
    const char *operator()(const char *name);
};

void lookup_name_demangled(void *addr, char *buf, size_t len);

/**@}*/
}

#endif /* DEMANGLE_HH_ */
