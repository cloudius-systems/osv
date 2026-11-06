/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * Linux stand-in for OSv's libc/stdio/stdio_impl.h: OSv's FILE (as osv_FILE, the fields OSv's
 * printf and scanf code uses) for scripts/locale-oracle/numeric-diff (build.sh). */
#ifndef OSV_SHIM_STDIO_IMPL_H
#define OSV_SHIM_STDIO_IMPL_H
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <stdbool.h>
#include <locale.h>
#include <sys/types.h>
#include "rename.h"
#define UNGET 8
#define FLOCK(f) int __need_unlock = 0
#define FUNLOCK(f) (void)__need_unlock
#define F_PERM 1
#define F_NORD 4
#define F_NOWR 8
#define F_EOF 16
#define F_ERR 32
#define F_SVB 64
#define F_APP 128
#define F_WBUF 256
typedef struct osv_FILE osv_FILE;
struct osv_FILE {
	unsigned flags;
	unsigned char *rpos, *rend;
	int (*close)(osv_FILE *);
	unsigned char *wend, *wpos;
	unsigned char *wbase;
	size_t (*read)(osv_FILE *, unsigned char *, size_t);
	size_t (*write)(osv_FILE *, const unsigned char *, size_t);
	unsigned char *buf;
	size_t buf_size;
	signed char mode;
	signed char lbf;
	int lock;
	void *cookie;
	unsigned char *shend;
	off_t shlim, shcnt;
	bool no_locking;
	locale_t locale;
	wint_t (*wput)(wchar_t, osv_FILE *);
	wint_t (*wget)(osv_FILE *);
	wint_t (*wunget)(wint_t, osv_FILE *);
};
#define FILE osv_FILE
hidden int __toread(FILE *);
hidden int __towrite(FILE *);
hidden int __uflow(FILE *);
hidden size_t __fwritex(const unsigned char *, size_t, FILE *);
hidden size_t __string_read(FILE *, unsigned char *, size_t);
int osv_fprintf(FILE *, const char *, ...);
int osv_fscanf(FILE *, const char *, ...);
wint_t osv_fputwc(wchar_t, FILE *);
int osv_fwide(FILE *, int);
wint_t osv_getwc(FILE *);
wint_t osv_ungetwc(wint_t, FILE *);
int osv_vfprintf(FILE *, const char *, va_list);
int osv_vfscanf(FILE *, const char *, va_list);
int osv_vfwprintf(FILE *, const wchar_t *, va_list);
int osv_vfwscanf(FILE *, const wchar_t *, va_list);
int osv_vsnprintf(char *, size_t, const char *, va_list);
int osv_vswprintf(wchar_t *, size_t, const wchar_t *, va_list);
int osv_vsscanf(const char *, const char *, va_list);
int osv_vswscanf(const wchar_t *, const wchar_t *, va_list);
#endif
