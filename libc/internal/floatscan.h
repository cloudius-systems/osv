/*
 * Copyright © 2005-2020 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Modified for OSv from musl 1.2.1 src/internal/floatscan.h: adds
 * __osv_floatscan and __osv_intscan (the locale's decimal point, and
 * scanf's ' grouping).
 */
#ifndef FLOATSCAN_H
#define FLOATSCAN_H

#include <stdio.h>

hidden long double __floatscan(FILE *, int, int);
struct osv_num;
/* nm: the numeric items (osv-numeric.h), NULL for the thread's current
 * locale's; grouped: 0, or scanf's ' flag: OSV_GRP_NARROW for vfscanf
 * (the leftmost group may have strlen(separator) - 1 extra digits; the
 * groups of an integer are not checked if the separator is multibyte) or
 * OSV_GRP_WIDE for vfwscanf (as glibc 2.41's, measured) */
#define OSV_GRP_NARROW 1
#define OSV_GRP_WIDE 2
hidden long double __osv_floatscan(FILE *, int prec, int pok, const struct osv_num *nm, int grouped);
/* gotdig (if not NULL) is set when a grouped decimal number had a digit:
 * glibc stores it even if a separator cut by the field width gave the
 * digit back (osv-scan.h, __osv_match_int) */
hidden unsigned long long __osv_intscan(FILE *, unsigned base, int pok, unsigned long long lim,
	const struct osv_num *nm, int grouped, int *gotdig);

#endif
