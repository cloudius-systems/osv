/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <bsd/porting/netport.h> /* __dead2 defined here */
#include <xen/hypervisor.h>

#include "xenconsole.hh"

namespace console {

void XEN_Console::write(const char *str, size_t len) {
	HYPERVISOR_console_write(str, len);
}

void XEN_Console::dev_start()
{
}

void XEN_Console::flush()
{
}

bool XEN_Console::input_ready()
{
	return false; /*TODO: support input */
}

char XEN_Console::readch() {
    return '\0'; /*TODO: support input */
}

}
