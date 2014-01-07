/*
* Copyright (C) 2013 Cloudius Systems, Ltd.
*
* This work is open source software, licensed under the terms of the
* BSD license as described in the LICENSE file in the top-level directory.
*/

#include <string.h>
#include <iostream>

// This test assures that the "memmove" function works as expected in the various
// situations it can be used at.
void pass_if(const char *got, const char *expected, int size)
{
    if (strncmp(expected, got, size)) {
        std::cerr << "ERROR: got ";
        std::cerr << got;
        std::cerr << " but expected ";
        std::cerr << expected;
        std::cerr << "\n";
        exit(1);
    }
}

int main()
{
    // addr(src) < addr(dest), size power of 2
    char buf1[9] = "12344321";
    memmove(&buf1[4], &buf1, 4);
    pass_if(buf1, "12341234", 8);

    // addr(src) < addr(dest), size not power of 2
    char buf2[11] = "1234554321";
    memmove(&buf2[5], &buf2, 5);
    pass_if(buf2, "1234512345", 10);

    // addr(dest) < addr(src), size power of 2
    char buf3[9] = "12344321";
    memmove(&buf3, &buf3[4], 4);
    pass_if(buf3, "43214321", 8);

    // addr(dest) < addr(src), size not power of 2
    char buf4[11] = "1234554321";
    memmove(&buf4, &buf4[5], 5);
    pass_if(buf4, "5432154321", 10);

    // Moving to the middle of the string
    char buf5[9] = "12344321";
    memmove(&buf5, &buf5[2], 4);
    pass_if(buf5, "34434321", 8);

    // Moving to the middle of the string, inverting dst and src
    char buf6[11] = "1234554321";
    memmove(&buf6[2], &buf6, 5);
    pass_if(buf6, "1212345321", 10);

    // Moving to the middle of string, whole word.
    char buf7[17] = "1234567887654321";
    memmove(&buf7, &buf7[2], 8);
    pass_if(buf7, "3456788787654321", 16);

    // Moving to the middle of string, bigger than whole word.
    char buf8[19] = "123456789987654321";
    memmove(&buf8[2], &buf8, 9);
    pass_if(buf8, "121234567897654321", 18);

    // This test is designed to make sure that memmove also works with
    // non-aligned addresses. Because we will loop over 8 possible starting
    // points, we will guaranteedly test all possible 8-bit alignments
    // regardless of which address our array starts at.
    //
    // The first test aligns keeps the destination intact, and varies the source
    char buf_loop[17]  = "1234567890000000";
    for (int i = 0; i < 8; ++i) {
        char buf_tmp[18];

        const char *loop_results[] = {
            "1234567812345678",
            "1234567823456789",
            "1234567834567890",
            "1234567845678900",
            "1234567856789000",
            "1234567867890000",
            "1234567878900000",
            "1234567889000000",
        };

        strncpy(buf_tmp, buf_loop, 18);
        memmove(&buf_tmp[8], &buf_tmp[i], 8);
        pass_if(buf_tmp, loop_results[i], 16);
    }

    // The second test varies the destination and keeps the source.
    char buf_loop2[17] = "0000000012345678";
    for (int i = 0; i < 8; ++i) {
        char buf_tmp[18];

        const char *loop_results[] = {
            "1234567812345678",
            "0123456782345678",
            "0012345678345678",
            "0001234567845678",
            "0000123456785678",
            "0000012345678678",
            "0000001234567878",
            "0000000123456788",
        };

        strncpy(buf_tmp, buf_loop2, 18);
        memmove(&buf_tmp[i], &buf_tmp[8], 8);
        pass_if(buf_tmp, loop_results[i], 16);
    }

    std::cerr << "PASSED\n";
    return 0;
}
