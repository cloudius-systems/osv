/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DUMP_HH
#define DUMP_HH

struct exception_frame;

void dump_registers(exception_frame* ef);

#endif /* DUMP_HH */
