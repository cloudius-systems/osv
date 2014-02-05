/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <bsd/sys/cddl/compat/opensolaris/sys/atomic.h>
#include <atomic>

template <typename T>
inline
volatile std::atomic<T>* as_atomic(volatile T* p)
{
    return reinterpret_cast<volatile std::atomic<T>*>(p);
}

void
atomic_add_64(volatile uint64_t *target, int64_t delta)
{
    as_atomic(target)->fetch_add(delta, std::memory_order_relaxed);
}

uint64_t
atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
{
    return as_atomic(target)->fetch_add(delta, std::memory_order_relaxed) + delta;
}

#if defined(__powerpc__) || defined(__arm__) || defined(__mips__)
void
atomic_or_8(volatile uint8_t *target, uint8_t value)
{
	mtx_lock(&atomic_mtx);
	*target |= value;
	mtx_unlock(&atomic_mtx);
}
#endif

uint8_t
atomic_or_8_nv(volatile uint8_t *target, uint8_t value)
{
    return as_atomic(target)->fetch_or(value, std::memory_order_relaxed) | value;
}

uint64_t
atomic_cas_64(volatile uint64_t *target, uint64_t cmp, uint64_t newval)
{
    as_atomic(target)->compare_exchange_strong(cmp, newval, std::memory_order_relaxed);
    return cmp;
}

uint32_t
atomic_cas_32(volatile uint32_t *target, uint32_t cmp, uint32_t newval)
{
    as_atomic(target)->compare_exchange_strong(cmp, newval, std::memory_order_relaxed);
    return cmp;
}

void
membar_producer(void)
{
    std::atomic_thread_fence(std::memory_order_release);
}
