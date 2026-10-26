/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// GNU message catalogs (.mo files) and their plural expressions, from the
// public file-format description: magic 0x950412de in either byte order, a
// revision, the number of strings N, the offsets of the original and the
// translation tables (N pairs of length and offset each), the size and the
// offset of an optional hash table. Nothing here touches files or locale
// state; intl.cc does. scripts/locale-oracle/messages/fuzz-mo.cc fuzzes it.

#ifndef OSV_MO_H
#define OSV_MO_H

#include <stddef.h>
#include <stdint.h>

#define OSV_MO_HIDDEN __attribute__((visibility("hidden")))

struct osv_mo {
    const char *base;
    size_t size;
    uint32_t n, otab, ttab, hsize, htab;
    bool swap;
    bool hash_only; // minor revision != 0: found only through the hash table
};

// Validate the header and the table bounds of size bytes at base. Strings
// are checked when they are used.
OSV_MO_HIDDEN bool osv_mo_open(const char *base, size_t size, osv_mo *m) noexcept;

// The translation of msgid (its forms separated by NULs, *len bytes before
// the final NUL), or nullptr.
OSV_MO_HIDDEN const char *osv_mo_find(const osv_mo *m, const char *msgid,
                                      size_t *len, uint32_t *index) noexcept;

// A plural expression, parsed into a tree (nodes[root]); count == 0 is the
// default rule (n != 1, two forms). Evaluated without recursion, depth
// frames; the operands of ?:, || and && only when selected (glibc's 1 || 5/0
// is 1).
struct osv_plural_node {
    uint64_t v;        // a literal
    uint32_t a, b, c;  // operands
    uint8_t op;
};
struct osv_plural {
    osv_plural_node *nodes;
    uint32_t count, root, depth;
    unsigned long nplurals;
};

// Parse the expression at s, which ends at ';', '\n' or NUL, with glibc's
// grammar and limits (C operators ?: || && == != < > <= >= + - * / % ! and
// parentheses, decimal literals and n, 64-bit unsigned; at most 9999 parser
// stack entries). False, with p unchanged, if it does not parse.
OSV_MO_HIDDEN bool osv_plural_compile(const char *s, osv_plural *p) noexcept;

// nplurals= and plural= from a catalog header (each found anywhere in it, as
// glibc does), else the default rule.
OSV_MO_HIDDEN void osv_plural_from_header(const char *header, osv_plural *p) noexcept;

// The form index for n: 0 when the value is not below nplurals; the default
// rule's value if the expression divides by zero (glibc raises SIGFPE).
OSV_MO_HIDDEN unsigned long osv_plural_index(const osv_plural *p, unsigned long n) noexcept;

// For the fuzz harness: release what osv_plural_compile() allocated.
OSV_MO_HIDDEN void osv_plural_free(osv_plural *p) noexcept;

#endif
