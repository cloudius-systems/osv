/*
 * Copyright (C) 2013 Nodalink, SARL.
 * Copyright (C) 2014 Cloudius Systems.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __OSV_COMMANDS_HH__
#define __OSV_COMMANDS_HH__

#include <string>
#include <vector>

extern int __argc;
extern char** __argv;

namespace osv {

std::vector<std::vector<std::string> >
parse_command_line(const std::string line, bool &ok);

int parse_cmdline(char *p);
}

#endif // !__OSV_COMMANDS_HH__
