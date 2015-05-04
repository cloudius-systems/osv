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
#include <osv/condvar.h>
#include <atomic>

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
    void request_memory(size_t s) { _pending.fetch_add(s); _blocked.wake_one(); }
    void release_memory(size_t s) { _pending_release.fetch_add(s); _blocked.wake_one(); }
    bool ballooning() { return _pending.load() > 0; }
    virtual ~jvm_balloon_shrinker();
private:
    void _release_memory(JNIEnv *env, size_t s);
    size_t _request_memory(JNIEnv *env, size_t s);
    void _thread_loop();
    JavaVM *_vm;
    int _attach(JNIEnv **env);
    void _detach(int status);
    size_t _total_heap;
    unsigned int _soft_max_balloons;
    sched::thread *_thread;
    condvar _blocked;
    std::atomic<size_t> _pending = {0};
    std::atomic<size_t> _pending_release = {0};
};

bool jvm_balloon_fault(balloon_ptr b, exception_frame *ef, mmu::jvm_balloon_vma *vma);

namespace memory {
    void return_jvm_heap(size_t size);
    void reserve_jvm_heap(size_t size);
    ssize_t jvm_heap_reserved();
    void jvm_balloon_adjust_memory(size_t threshold);
};
void jvm_balloon_voluntary_return();
#endif
