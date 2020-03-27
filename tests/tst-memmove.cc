/*
* Copyright (C) 2013 Cloudius Systems, Ltd.
*
* This work is open source software, licensed under the terms of the
* BSD license as described in the LICENSE file in the top-level directory.
*/

#include <string.h>
#include <iostream>
#include <cassert>

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

// This is a naive implementation of memmove just to verify
// that real memmove works correctly
static void* memmove_model(void *dest, const void *src, size_t n)
{
    char *d = (char*)dest;
    const char *s = (char*)src;

    if (d==s || n <= 0) return d;
    if (d<s) {
        while (n--) {
            *(d++) = *(s++);
        }
    } else {
        s = s + n - 1;
        d = d + n - 1;
        while (n--) {
            *(d--) = *(s--);
        }
    }
    return dest;
}

#define BUF_SIZE 0x4000

// Test memmove by comparing results of real memmove and the one above
static void memmove_test(int dest_offset, int src_offset, size_t n)
{
    char *buf1 = (char*)malloc(BUF_SIZE);
    char *buf2 = (char*)malloc(BUF_SIZE);

    for (int i = 0; i < BUF_SIZE; i++) {
        buf1[i] = buf2[i] = i % 256;
    }

    memmove_model(buf1 + dest_offset, buf1 + src_offset, n);
    memmove(buf2 + dest_offset, buf2 + src_offset, n);
    assert(0 == memcmp(buf1, buf2, BUF_SIZE));

    free(buf2);
    free(buf1);
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

    // Test some explicit cases where some of them would fail before a fix in memmove_backwards()
    memmove_test(381, 0, 8);
    memmove_test(4, 0, 2303);
    memmove_test(40, 0, 2692);
    memmove_test(10, 0, 2732);
    memmove_test(424, 0, 2746);
    memmove_test(10, 0, 3618);
    memmove_test(415, 0, 3646);
    memmove_test(6, 0, 4057);
    memmove_test(6, 0, 4063);
    memmove_test(115, 0, 4075);
    memmove_test(4, 0, 4190);
    memmove_test(761, 0, 4202);
    memmove_test(587, 0, 5773);
    memmove_test(47, 0, 6356);
    memmove_test(6, 0, 403);
    memmove_test(10, 0, 6417);
    memmove_test(757, 0, 6600);
    memmove_test(703, 0, 8585);
    memmove_test(206, 0, 9284);
    memmove_test(720, 0, 9494);
    memmove_test(4, 0, 10210);
    memmove_test(597, 0, 11855);
    memmove_test(6, 0, 13520);
    memmove_test(4, 0, 13526);
    memmove_test(125, 0, 14572);

    // Test random overlapping memmove scenarios
    int n;
    for (int i = 0; i < 1000; i++) {
        int destOffset = rand() % BUF_SIZE;
        int srcOffset = rand() % BUF_SIZE;
        // Calculate number of bytes so that source and dest overlap
        if (destOffset < srcOffset) {
            n = std::min(srcOffset - destOffset + 128, BUF_SIZE - srcOffset);
            assert(srcOffset + n <= BUF_SIZE);
        } else {
            n = std::min(destOffset - srcOffset + 128, BUF_SIZE - destOffset);
            assert(destOffset + n <= BUF_SIZE);
        }
        memmove_test(destOffset, srcOffset, n);
    }

    std::cerr << "PASSED\n";
    return 0;
}
