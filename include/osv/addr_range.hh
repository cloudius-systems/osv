/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ADDR_RANGE_HH
#define ADDR_RANGE_HH

class addr_range {
public:
    addr_range(uintptr_t start, uintptr_t end)
        : _start(start), _end(end) {}
    uintptr_t start() const { return _start; }
    uintptr_t end() const { return _end; }
private:
    uintptr_t _start;
    uintptr_t _end;
};

#endif
