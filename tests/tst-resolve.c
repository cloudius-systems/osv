/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stdio.h>
#define TARGET_VALUE 0x05050505

// Check that OSV symbols like "debug" and "condvar_wait" don't prevent us
// from using these names in the application (a shared object)
int nonexistant = TARGET_VALUE;
int debug = TARGET_VALUE;
int condvar_wait = TARGET_VALUE;

// On the other hand, check that symbols that we define here don't mess with
// OSV's internal implementation. For example, OSV's time() uses
// clock_gettime(), so see it uses its own code and not crap we define here.
// I.e., because OSV is the main executable (not a shared object), it was
// linked to find its own symbols inside itself and not in the shared objects
// like this one.
int clock_gettime = 0;


int fail = 0;
void check(const char *name, int val)
{
    if (val != TARGET_VALUE) {
        printf("Failed:\t%s = 0x%08x\n", name, val);
        fail++;
    } else {
        printf("Success:\t%s = 0x%08x\n", name, val);
    }
}

int time(int *t);

#define CHECK(v) check(#v, v)

int main(int ac, char** av)
{
    printf("Target value:\t0x%08x\n", TARGET_VALUE);
    CHECK(nonexistant);
    CHECK(debug);
    CHECK(condvar_wait);

    printf("The time:\t%d\n", time(0));

    if (fail) {
        printf("%d failures.\n", fail);
    } else {
        printf("success.\n");
    }

    return fail;
}
