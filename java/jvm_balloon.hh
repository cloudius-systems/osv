/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef JVM_BALLOON_HH_
#define JVM_BALLOON_HH_

#include <jni.h>
#include <osv/mempool.hh>
#include "exceptions.hh"
#include <osv/mmu.hh>

// We will divide the balloon in units of 128Mb. That should increase the likelyhood
// of having hugepages mapped in and out of it.
//
// Using constant sized balloons should help with the process of giving memory
// back to the JVM, since we don't need to search the list of balloons until
// we find a balloon of the desired size: any will do.
constexpr size_t balloon_size = (128ULL << 20);
// FIXME: Can probably do better than this. We are counting 4Mb before 1Gb to
// account for ROMs and the such. 4Mb is probably too much (in kvm with no vga
// we lose around 400k), but it doesn't hurt.
constexpr size_t balloon_min_memory = (1ULL << 30) - (4 << 20);
constexpr size_t balloon_alignment = mmu::huge_page_size;

class jvm_balloon_shrinker {
public:
    explicit jvm_balloon_shrinker(JavaVM *vm);
    size_t request_memory(size_t s);
    size_t release_memory(size_t s);
    virtual ~jvm_balloon_shrinker();
private:
    JavaVM *_vm;
    int _attach(JNIEnv **env);
    void _detach(int status);
    // FIXME: It can grow, but we will ignore it for now.
    size_t _total_heap;
};

bool jvm_balloon_fault(balloon_ptr b, exception_frame *ef, mmu::jvm_balloon_vma *vma);

namespace memory {
    void return_jvm_heap(size_t size);
    void reserve_jvm_heap(size_t size);
    ssize_t jvm_heap_reserved();
    void jvm_balloon_adjust_memory(size_t threshold);
};
#endif
