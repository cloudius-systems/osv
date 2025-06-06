/*
 * Copyright (C) 2025 Reliable System Software, Technische Universität Braunschweig.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stdio.h>

extern void called_from_b(void);

void
called_from_a(void)
{
	puts(__func__);
}

void
func_b(void)
{
	printf("%p\n", &called_from_b);
	called_from_b();
}
