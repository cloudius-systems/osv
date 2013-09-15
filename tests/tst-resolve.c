/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stdio.h>

// Check that OSV symbols like "debug" and "condvar_wait" don't prevent us
// from using these names in the application (a shared object)
int nonexistant = 0;
int debug = 0;
int condvar_wait = 0;

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
    if (val) {
        printf("Failed: %s = %d\n", name, val);
        fail++;
    } else {
        printf("Success: %s = %d\n", name, val);
    }
}

int time(int *t);

#define CHECK(v) check(#v, v)

int main(int ac, char** av)
{
    CHECK(nonexistant);
    CHECK(debug);
    CHECK(condvar_wait);

    printf("The time: %d\n", time(0));

    if (fail) {
        printf("%d failures.\n", fail);
    } else {
        printf("success.\n");
    }

    return fail;
}
