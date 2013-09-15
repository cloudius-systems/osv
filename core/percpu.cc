/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <osv/percpu.hh>
#include <bitset>
#include <debug.hh>

static constexpr size_t dynamic_percpu_max = 65536;

union dynamic_percpu_buffer {
    long align;
    char buf[dynamic_percpu_max];
};

static PERCPU(dynamic_percpu_buffer, buffer);
static std::bitset<dynamic_percpu_max> dynamic_percpu_allocated;
static mutex mtx;

size_t dynamic_percpu_base()
{
    return reinterpret_cast<size_t>(&buffer._var.buf);
}

size_t dynamic_percpu_alloc(size_t size, size_t align)
{
    std::lock_guard<mutex> guard(mtx);
    for (size_t i = 0; i < dynamic_percpu_max; i += align) {
        size_t j = 0;
        for (; j < size; ++j) {
            if (dynamic_percpu_allocated.test(i + j)) {
                break;
            }
        }
        if (j == size) {
            for (j = 0; j < size; ++j) {
                dynamic_percpu_allocated.set(i + j, true);
            }
            return dynamic_percpu_base() + i;
        }
    }
    abort("exhausted dynamic percpu pool");
}

void dynamic_percpu_free(size_t offset, size_t size)
{
    offset -= dynamic_percpu_base();
    std::lock_guard<mutex> guard(mtx);
    for (size_t j = 0; j < size; ++j) {
        dynamic_percpu_allocated.set(offset + j, false);
    }
}

