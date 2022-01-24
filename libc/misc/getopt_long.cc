#include <stddef.h>
#include <getopt.h>
#include <stdio.h>
#include <osv/export.h>
#include "getopt.hh"

extern "C" {

extern int __optpos, __optreset;

static int __getopt_long(int argc, char *const *argv, const char *optstring, const struct option *longopts, int *idx, int longonly)
{
	getopt_caller_vars_copier guard;

	if (!optind || __optreset) {
		__optreset = 0;
		__optpos = 0;
		optind = 1;
	}
	if (optind >= argc || !argv[optind] || argv[optind][0] != '-') return -1;
	if ((longonly && argv[optind][1]) ||
		(argv[optind][1] == '-' && argv[optind][2]))
	{
		int i;
		for (i=0; longopts[i].name; i++) {
			const char *name = longopts[i].name;
			char *opt = argv[optind]+1;
			if (*opt == '-') opt++;
			for (; *name && *name == *opt; name++, opt++);
			if (*name || (*opt && *opt != '=')) continue;
			if (*opt == '=') {
				if (!longopts[i].has_arg) continue;
				optarg = opt+1;
			} else {
				if (longopts[i].has_arg == required_argument) {
					if (!(optarg = argv[++optind]))
						return ':';
				} else optarg = NULL;
			}
			optind++;
			if (idx) *idx = i;
			if (longopts[i].flag) {
				*longopts[i].flag = longopts[i].val;
				return 0;
			}
			return longopts[i].val;
		}
		if (argv[optind][1] == '-') {
			optind++;
			return '?';
		}
	}
	return __getopt(argc, argv, optstring);
}

OSV_LIBC_API
int getopt_long(int argc, char *const *argv, const char *optstring, const struct option *longopts, int *idx)
{
	return __getopt_long(argc, argv, optstring, longopts, idx, 0);
}

OSV_LIBC_API
int getopt_long_only(int argc, char *const *argv, const char *optstring, const struct option *longopts, int *idx)
{
	return __getopt_long(argc, argv, optstring, longopts, idx, 1);
}
}
