/*
 * Copyright (C) 2026 Greg Burd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * lmp: libc's own messages, one line each. Usage: lmp <locale|-> [all|probe]
 *   all    strerror(-2..140), strsignal(-1..66), gai_strerror(-110..1),
 *          hstrerror(-2..6), regerror(0..16)
 *   probe  every other libc function that prints or returns a message
 * The locale is set with setlocale(LC_ALL, name) ("-": no setlocale).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <err.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <langinfo.h>
#include <libintl.h>
#include <locale.h>
#include <netdb.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put(const char *k, long n, const char *s)
{
	printf("%s %ld ", k, n);
	if (!s) { printf("(null)\n"); return; }
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		if (*p < 32 || *p == '\\') printf("\\x%02x", *p); else putchar(*p);
	putchar('\n');
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "-")) setlocale(LC_ALL, argv[1]);
	const char *what = argc > 2 ? argv[2] : "all";
	if (!strcmp(what, "all")) {
		for (int e = -2; e <= 140; e++) put("strerror", e, strerror(e));
		for (int s = -1; s <= 66; s++) put("strsignal", s, strsignal(s));
		for (int g = -110; g <= 1; g++) put("gai_strerror", g, gai_strerror(g));
		for (int h = -2; h <= 6; h++) put("hstrerror", h, hstrerror(h));
		for (int r = 0; r <= 16; r++) { char eb[256]; size_t n = regerror(r, NULL, eb, sizeof eb); put("regerror", r, eb); put("regerror-len", r, n == strlen(eb) + 1 ? "ok" : "BAD"); }
		return 0;
	}
	char buf[256];
	setvbuf(stderr, NULL, _IONBF, 0);
	dup2(1, 2);
	put("strerror_r", 2, strerror_r(2, buf, sizeof buf));
	put("strerror_r", 999, strerror_r(999, buf, sizeof buf));
	
	locale_t c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	put("strerror_l(C)", 2, strerror_l(2, c));
	locale_t de = newlocale(LC_ALL_MASK, "de_DE.UTF-8", (locale_t)0);
	if (de) put("strerror_l(de)", 2, strerror_l(2, de));
	put("strerrordesc_np", 2, strerrordesc_np(2));
	put("strerrorname_np", 2, strerrorname_np(2));
	put("sigdescr_np", 2, sigdescr_np(2));
	put("sigabbrev_np", 2, sigabbrev_np(2));
	put("hstrerror", 1, hstrerror(1));
	put("hstrerror", 99, hstrerror(99));
	{ regex_t r; char eb[200]; int rc = regcomp(&r, "a(", REG_EXTENDED); regerror(rc, &r, eb, sizeof eb); put("regerror", rc, eb);
	  /* glibc aborts on a code past REG_ERPAREN (16): measured */
	  for (int i = 0; i <= 16; i++) { regerror(i, NULL, eb, sizeof eb); put("regerror", i, eb); } }
	errno = 2; printf("perror: "); fflush(stdout); perror("P");
	printf("psignal: "); fflush(stdout); psignal(2, "S");
	printf("psignal-null: "); fflush(stdout); psignal(2, NULL);
	{ siginfo_t si; memset(&si, 0, sizeof si); si.si_signo = 11; si.si_code = 1;
	  printf("psiginfo: "); fflush(stdout); psiginfo(&si, "I"); }
	printf("warn: "); fflush(stdout); errno = 2; warn("W%d", 1);
	printf("warnx: "); fflush(stdout); warnx("X");
	printf("error: "); fflush(stdout); error(0, 2, "E");
	printf("herror: "); fflush(stdout); herror("H");
	{ char *av[] = { "prog", "-z", "--bogus", "-a", NULL }; int ac = 4;
	  struct option lo[] = { { "need", 1, 0, 'n' }, { 0 } };
	  opterr = 1; printf("getopt:\n"); fflush(stdout);
	  while (getopt_long(ac, av, "a:", lo, NULL) != -1); }
	put("nl_langinfo(YESEXPR)", 0, nl_langinfo(YESEXPR));
	put("dgettext(libc,Success)", 0, dgettext("libc", "Success"));
	put("gettext(Success)", 0, gettext("Success"));
	put("printf %m", 0, (errno = 2, (snprintf(buf, sizeof buf, "%m"), buf)));
	return 0;
}
