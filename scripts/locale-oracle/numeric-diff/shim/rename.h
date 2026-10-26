/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * OSv's stdio and strtod names as osv_..., defined after glibc's headers (see build.sh). */
#ifndef OSV_SHIM_RENAME_H
#define OSV_SHIM_RENAME_H
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define vfprintf osv_vfprintf
#define vsnprintf osv_vsnprintf
#define vfwprintf osv_vfwprintf
#define vswprintf osv_vswprintf
#define vfscanf osv_vfscanf
#define vsscanf osv_vsscanf
#define vfwscanf osv_vfwscanf
#define vswscanf osv_vswscanf
#define __isoc99_vfscanf osv___isoc99_vfscanf
#define __isoc99_vsscanf osv___isoc99_vsscanf
#define __isoc99_vfwscanf osv___isoc99_vfwscanf
#define __isoc99_vswscanf osv___isoc99_vswscanf
#define strtod osv_strtod
#define strtof osv_strtof
#define strtold osv_strtold
#define strtod_l osv_strtod_l
#define strtof_l osv_strtof_l
#define strtold_l osv_strtold_l
#define __strtod_l osv___strtod_l
#define __strtof_l osv___strtof_l
#define __strtold_l osv___strtold_l
#define fprintf osv_fprintf
#define fscanf osv_fscanf
#define fputwc osv_fputwc
#define fwide osv_fwide
#define __uflow osv___uflow
#define __overflow osv___overflow
#define __toread osv___toread
#define __towrite osv___towrite
#define __fwritex osv___fwritex
#define __string_read osv___string_read
double osv_strtod(const char *, char **);
float osv_strtof(const char *, char **);
long double osv_strtold(const char *, char **);
#endif
