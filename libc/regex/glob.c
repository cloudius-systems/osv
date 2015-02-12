#include "libc.h"

static int is_literal(const char *p, int useesc)
{
	int bracket = 0;
	for (; *p; p++) {
		switch (*p) {
		case '\\':
			if (!useesc) break;
		case '?':
		case '*':
			return 0;
		case '[':
			bracket = 1;
			break;
		case ']':
			if (bracket) return 0;
			break;
		}
	}
	return 1;
}

/* TODO: Should be up-streamed to musl */
int __glob_pattern_p(const char *pattern, int quote)
{
    return !is_literal(pattern, !quote);
}

weak_alias(__glob_pattern_p, glob_pattern_p);
