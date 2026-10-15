/*
 * Copyright (C) 2026 Greg Burd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * gtp: a gettext probe. Each argument is one operation, run in order:
 *   L<cat>=<name>  setlocale (cat: A C N T O M = ALL CTYPE NUMERIC TIME COLLATE MESSAGES)
 *   B<dom>=<dir>   bindtextdomain (dir "-" = NULL)      T<dom>  textdomain ("-" = NULL)
 *   S<dom>=<cs>    bind_textdomain_codeset ("-" = NULL)
 *   g<id>          gettext            d<dom>:<id>      dgettext
 *   c<dom>:<id>:<cat>  dcgettext      n<dom>:<id1>:<id2>:<n>  dngettext ("" dom = NULL)
 *   e<NAME>=<val>  setenv (val "-" = unsetenv)
 *   E<n>           errno is set to n (not 0) before the next operation
 *   D<dir>         chdir
 *   V<name>        uselocale(newlocale(LC_CTYPE_MASK, name, duplocale(global)))
 *   U<name>        uselocale(newlocale(LC_ALL_MASK, name)); "-" = LC_GLOBAL_LOCALE
 * \x01 in an argument stands for \004 (the msgctxt separator).
 * Each result is printed as one line: op, returned string (escaped) or (null), errno.
 */
#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int cat(const char *s)
{
	switch (*s) {
	case 'A': return LC_ALL; case 'C': return LC_CTYPE; case 'N': return LC_NUMERIC;
	case 'T': return LC_TIME; case 'O': return LC_COLLATE; case 'M': return LC_MESSAGES;
	}
	return atoi(s);
}

static void out(const char *op, const char *r)
{
	printf("%s => ", op);
	if (!r) { printf("(null) errno=%d\n", errno); return; }
	putchar('"');
	for (const unsigned char *p = (const unsigned char *)r; *p; p++)
		if (*p < 32 || *p == '"' || *p == '\\') printf("\\x%02x", *p); else putchar(*p);
	printf("\" errno=%d\n", errno);
}

static char *nul(char *s) { return strcmp(s, "-") ? s : NULL; }

int main(int argc, char **argv)
{
	int preset = 0;
	for (int i = 1; i < argc; i++) {
		char *a = strdup(argv[i]), *op = argv[i], *eq, *f[4] = {0};
		for (char *p = a; *p; p++) if (*p == 1) *p = 4;
		char *b = a + 1;
		int nf = 0;
		f[nf++] = b;
		for (char *p = b; *p && nf < 4; p++) if (*p == ':') { *p = 0; f[nf++] = p + 1; }
		errno = preset;
		preset = 0;
		switch (a[0]) {
		case 'L': eq = strchr(argv[i], '='); out(op, setlocale(cat(argv[i] + 1), eq + 1)); break;
		case 'B': eq = strchr(b, '='); *eq = 0; out(op, bindtextdomain(b, nul(eq + 1))); break;
		case 'S': eq = strchr(b, '='); *eq = 0; out(op, bind_textdomain_codeset(b, nul(eq + 1))); break;
		case 'T': out(op, textdomain(nul(b))); break;
		case 'g': out(op, gettext(argv[i][1] ? b : "")); break;
		case 'd': out(op, dgettext(*f[0] ? f[0] : NULL, f[1])); break;
		case 'c': out(op, dcgettext(*f[0] ? f[0] : NULL, f[1], cat(f[2]))); break;
		case 'n': out(op, dngettext(*f[0] ? f[0] : NULL, f[1], f[2], strtoul(f[3], 0, 10))); break;
		case 'V': {
			locale_t l = newlocale(LC_CTYPE_MASK, b, duplocale(LC_GLOBAL_LOCALE));
			if (l) uselocale(l);
			out(op, l ? "ok" : NULL);
			break;
		}
		case 'D': out(op, chdir(b) ? NULL : "ok"); break;
		case 'E': preset = atoi(b); break;
		case 'U': {
			locale_t l = strcmp(b, "-") ? newlocale(LC_ALL_MASK, b, (locale_t)0) : LC_GLOBAL_LOCALE;
			if (l) uselocale(l);
			out(op, l ? "ok" : NULL);
			break;
		}
		case 'e': eq = strchr(b, '='); *eq = 0;
			if (strcmp(eq + 1, "-")) setenv(b, eq + 1, 1); else unsetenv(b);
			printf("%s\n", op); break;
		}
		fflush(stdout);
		free(a);
	}
	return 0;
}
