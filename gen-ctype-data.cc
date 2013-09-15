/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <iostream>
#include <ctype.h>

namespace {

    void do_ctype(std::ostream& os, int i, int (*f)(int), std::string name)
    {
	if (f(i)) {
	    os << " | " << name;
	}
    }
}

#define DO(x) do_ctype(std::cout, i, is##x, "_IS" #x)

int main(int ac, char **av)
{
    for (int i = -127; i < 256; ++i) {
	std::cout << "0";
	DO(alnum);
	DO(alpha);
	/* DO(ascii); */
	DO(blank);
	DO(cntrl);
	DO(digit);
	DO(graph);
	DO(lower);
	DO(print);
	DO(punct);
	DO(space);
	DO(upper);
	DO(xdigit);
	std::cout << ",\n";
    }
}
