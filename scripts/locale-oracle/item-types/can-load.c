/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* can-load <cat> <name>: does glibc's newlocale(1 << cat, name) succeed? */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    locale_t l = newlocale(1 << atoi(argv[1]), argv[2], 0);
    puts(l ? "ok" : "fail");
    return 0;
}
