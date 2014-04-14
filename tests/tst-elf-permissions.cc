/*
 * Copyright (C) 2014 Paweł Dziepak
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "tst-mmap.hh"

#include <iostream>
#include <cassert>
#include <cstdlib>

static int test_text_section() __attribute__((noinline));

// Setting attribute section results in gcc emitting gas directive
// .section with necessary flags appended. We do not want that since,
// depending on the linker ,that would either make .data section executable
// or cause an unnecessary warning that these flags are being ignored. The
// solution is to take advantage from the fact that gcc passes section name
// verbatim to the assembler and thus adding '#' makes whatever gcc appends
// to the directive ignored.
static int test_data_section() __attribute__((noinline, section(".data #")));

static int test_gnu_relro __attribute__((section(".got #")));

volatile int value = 123;
static int test_text_section()
{
    return value;
}

static int test_data_section()
{
    return value;
}

int main()
{
    std::cerr << "Running elf segments permissions tests\n";

    assert(try_execute(test_text_section));
    assert(!try_write(test_text_section));

    assert(try_write(test_data_section));
    assert(!try_execute(test_data_section));

    assert(try_read(&test_gnu_relro));
    assert(!try_write(&test_gnu_relro));

    std::cerr << "elf segments permissions tests succeeded\n";
}
