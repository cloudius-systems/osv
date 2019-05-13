/*
 * Copyright (C) 2019 Waldek Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * This source code is loosely based on the examples from GNU libs manuals
 * https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html
 * and https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html.
 */

#include <getopt.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cassert>
#include <cstring>

void
test_getopt(int argc, char *const argv[], int expected_aflag, int expected_bflag,
            const char *expected_cvalue, const char *expected_non_option_arg) {

    int aflag = 0;
    int bflag = 0;
    char *cvalue = NULL;
    int c;

    printf("Running test: %s ...\n", argv[0]);

    opterr = 0;
    optind = 0;

    while ((c = getopt(argc, argv, "abc:")) != -1)
        switch (c) {
            case 'a':
                aflag = 1;
                break;
            case 'b':
                bflag = 1;
                break;
            case 'c':
                cvalue = optarg;
                break;
            case '?':
                if (optopt == 'c')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint (optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr,
                            "Unknown option character `\\x%x'.\n",
                            optopt);
                assert(0);
            default:
                assert(0);
        }

    assert(expected_aflag == aflag);
    assert(expected_bflag == bflag);
    if (expected_cvalue && cvalue) {
        assert(strcmp(expected_cvalue, cvalue) == 0);
    } else {
        assert(!cvalue && !expected_cvalue);
    }

    if (expected_non_option_arg) {
        assert(optind + 1 == argc);
        assert(strcmp(expected_non_option_arg, argv[optind]) == 0);
    } else {
        assert(optind == argc);
    }
}

static int verbose_flag;

void
test_getopt_long(int argc, char *const argv[], int expected_aflag, int expected_bflag, const char *expected_cvalue,
                 const char *expected_dvalue, const char *expected_fvalue, int expected_verbose) {
    int c;
    int aflag = 0;
    int bflag = 0;
    char *cvalue = NULL;
    char *dvalue = NULL;
    char *fvalue = NULL;

    printf("Running test: %s ...\n", argv[0]);

    opterr = 0;
    optind = 0;

    while (1) {
        struct option long_options[] =
                {
                        /* These options set a flag. */
                        {"verbose", no_argument,       &verbose_flag, 1},
                        {"brief",   no_argument,       &verbose_flag, 0},
                        /* These options don’t set a flag.
                           We distinguish them by their indices. */
                        {"add",     no_argument,       0,             'a'},
                        {"append",  no_argument,       0,             'b'},
                        {"delete",  required_argument, 0,             'd'},
                        {"create",  required_argument, 0,             'c'},
                        {"file",    required_argument, 0,             'f'},
                        {0, 0,                         0,             0}
                };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long(argc, argv, "abc:d:f:",
                        long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
            case 0:
                /* If this option set a flag, do nothing else now. */
                if (long_options[option_index].flag != 0)
                    break;
                printf("option %s", long_options[option_index].name);
                if (optarg)
                    printf(" with arg %s", optarg);
                printf("\n");
                break;

            case 'a':
                aflag = 1;
                break;

            case 'b':
                bflag = 1;
                break;

            case 'c':
                printf("option -c with value `%s'\n", optarg);
                cvalue = optarg;
                break;

            case 'd':
                printf("option -d with value `%s'\n", optarg);
                dvalue = optarg;
                break;

            case 'f':
                printf("option -f with value `%s'\n", optarg);
                fvalue = optarg;
                break;

            case '?':
                /* getopt_long already printed an error message. */
                assert(0);

            default:
                assert(0);
        }
    }

    assert(expected_aflag == aflag);
    assert(expected_bflag == bflag);

    if (expected_cvalue && cvalue) {
        assert(strcmp(expected_cvalue, cvalue) == 0);
    } else {
        assert(!cvalue && !expected_cvalue);
    }

    if (expected_dvalue && dvalue) {
        assert(strcmp(expected_dvalue, dvalue) == 0);
    } else {
        assert(!dvalue && !expected_dvalue);
    }

    if (expected_fvalue && fvalue) {
        assert(strcmp(expected_fvalue, fvalue) == 0);
    } else {
        assert(!fvalue && !expected_fvalue);
    }

    assert(verbose_flag == expected_verbose);

    if (optind < argc) {
        printf("non-option ARGV-elements: ");
        while (optind < argc)
            printf("%s ", argv[optind++]);
        putchar('\n');
    }
}

int
main(int argc, char *argv[]) {
    char *const test1[] = {(char *) "tst-getopt1", nullptr};
    test_getopt(1, test1, 0, 0, nullptr, nullptr);

    char *const test2[] = {(char *) "tst-getopt2", (char *) "-a", (char *) "-b", nullptr};
    test_getopt(3, test2, 1, 1, nullptr, nullptr);

    char *const test3[] = {(char *) "tst-getopt3", (char *) "-ab", nullptr};
    test_getopt(2, test3, 1, 1, nullptr, nullptr);

    char *const test4[] = {(char *) "tst-getopt4", (char *) "-c", (char *) "foo", nullptr};
    test_getopt(3, test4, 0, 0, "foo", nullptr);

    char *const test5[] = {(char *) "tst-getopt5", (char *) "-cfoo", nullptr};
    test_getopt(2, test5, 0, 0, "foo", nullptr);

    char *const test6[] = {(char *) "tst-getopt6", (char *) "arg1", nullptr};
    test_getopt(2, test6, 0, 0, nullptr, "arg1");

    char *const test7[] = {(char *) "tst-getopt7", (char *) "-a", (char *) "arg1", nullptr};
    test_getopt(3, test7, 1, 0, nullptr, "arg1");

    char *const test8[] = {(char *) "tst-getopt8", (char *) "-c", (char *) "foo", (char *) "arg1", nullptr};
    test_getopt(4, test8, 0, 0, "foo", "arg1");

    char *const test9[] = {(char *) "tst-getopt9", (char *) "-a", (char *) "--", (char *) "-b", nullptr};
    test_getopt(4, test9, 1, 0, nullptr, "-b");

    char *const test10[] = {(char *) "tst-getopt10", (char *) "-a", (char *) "-", nullptr};
    test_getopt(3, test10, 1, 0, nullptr, "-");

    char *const long_test1[] = {(char *) "tst-getopt_long1", (char *) "-a", (char *) "--create", (char *) "bula",
                                nullptr};
    test_getopt_long(4, long_test1, 1, 0, "bula", nullptr, nullptr, 0);

    char *const long_test2[] = {(char *) "tst-getopt_long2", (char *) "-a", (char *) "--create", (char *) "bula",
                                (char *) "--append", (char *) "-f", (char *) "x.txt", (char *) "--verbose", nullptr};
    test_getopt_long(8, long_test2, 1, 1, "bula", nullptr, "x.txt", 1);

    return 0;
}
