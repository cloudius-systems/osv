// Tests for strfrom128() and strtof128() needed because of issue #1238.
// This test should pass on both OSv and on Linux with recent glibc with
// those two functions added.
// This test does NOT currently pass on OSv - we only have a stub
// implementation of these functions.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned int tests_total = 0, tests_failed = 0;

void report(const char* name, bool passed)
{
   static const char* status[] = {"FAIL", "PASS"};
   printf("%s: %s\n", status[passed], name);
   tests_total += 1;
   tests_failed += !passed;
}

int main(void)
{
    printf("Starting strfromf128()/strtof128() test\n");
    // It appears that gcc truncates floating literals to 64 bit, and
    // with "L" suffix, to 80 bits. To really get 128 bits, the "f128" suffix
    // is needed.
    __float128 pi = 3.14159265358979323846264338327950288419716939937510f128;
    // Successful path for strfromf128(), with 20 digits of precision
    // (precision which would not be achievable for 64-bit double):.
    // Note that the 20th digit is rounded (...46 is rounded to ...5).
    char buf[1024];
    int ret = strfromf128(buf, sizeof(buf), "%.20g", pi);
    report("strfromf128 returns 21", ret == 21);
    report("strfromf128 returns right string", !strcmp(buf, "3.1415926535897932385"));

    // Test strfromf128() with not enough place for the number, or just
    // enough place for the number but not for the final null. Still returns
    // the whole length.
    ret = strfromf128(buf, 10, "%.20g", pi);
    report("strfromf128 returns 21", ret == 21);
    ret = strfromf128(buf, 21, "%.20g", pi);
    report("strfromf128 returns 21", ret == 21);

    // Successful path for strtof128(), with endptr==null.
    // The result of converting spi to a number should be the same as pi
    // defined above - not less precision.
    const char* spi = "3.14159265358979323846264338327950288419716939937510 hi";
    __float128 npi = strtof128(spi, nullptr);
    report("strtof128 returns the right value", npi == pi);

    // With endptr!=null, we get the pointer to the end of the number
    char *endptr;
    npi = strtof128(spi, &endptr);
    report("strtof128 returns the right value", npi == pi);
    report("strtof128 returns the right end", (endptr - spi) == 52);

    printf("SUMMARY: %u tests / %u failures\n", tests_total, tests_failed);
    return !!tests_failed;
}
