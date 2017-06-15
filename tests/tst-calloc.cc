/*
* Copyright (C) 2017 XLAB d.o.o.
*
* This work is open source software, licensed under the terms of the
* BSD license as described in the LICENSE file in the top-level directory.
*/
// To compile on Linux, use: g++ -g -pthread -std=c++11 tests/tst-calloc.cc

#include <string.h>
#include <iostream>

// This test assures that the calloc function works as expected in the various
// situations it can be used at.
void pass_if(const char *got, const char *expected, int size, const char* msg)
{
    if (memcmp(expected, got, size)) {
        std::cerr << "FAIL: " << msg << "\n";
        std::cerr << "ERROR: got ";
        std::cerr << got;
        std::cerr << " but expected ";
        std::cerr << expected;
        std::cerr << "\n";
        exit(1);
    }
    else {
        std::cerr << "PASS: " << msg << "\n";
    }
}

int main()
{
    char *buf1;
    const char ref0[11] = "\0";
    const char refX[11] = "XXXXXXXXXX";
    int len1, len2;

    len1 = 1;
    len2 = 10;
    buf1 = (char*)calloc(len1, len2);
    pass_if(buf1, ref0, len1 * len2, "calloc(1, 10)");
    memset(buf1, 'X', len1 * len2);
    free(buf1);

    len1 = 2;
    len2 = 5;
    buf1 = (char*)calloc(len1, len2);
    pass_if(buf1, ref0, len1 * len2, "calloc(2, 5)");
    memset(buf1, 'X', len1 * len2);
    free(buf1);

    len1 = 1;
    len2 = 0;
    buf1 = (char*)calloc(len1, len2);
    pass_if(buf1, ref0, len1 * len2, "calloc(1, 0)");
    //memset(buf1, 'X', len1 * len2);
    free(buf1);

    len1 = 0;
    len2 = 1;
    buf1 = (char*)calloc(len1, len2);
    pass_if(buf1, ref0, len1 * len2, "calloc(0, 1)");
    memset(buf1, 'X', len1 * len2);
    free(buf1);

    std::cerr << "PASSED\n";
    return 0;
}
