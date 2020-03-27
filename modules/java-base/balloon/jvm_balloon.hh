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

class jvm_balloon_shrinker {
public:
    explicit jvm_balloon_shrinker(JavaVM *vm);
    void request_memory(size_t s) { _pending.fetch_add(s); _blocked.wake_one(); }
    void release_memory(size_t s) { _pending_release.fetch_add(s); _blocked.wake_one(); }
    bool ballooning() { return _pending.load() > 0; }
    virtual ~jvm_balloon_shrinker() {};
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

namespace memory {
class jvm_balloon_api_impl : public jvm_balloon_api {
public:
    explicit jvm_balloon_api_impl(JavaVM *jvm);
    virtual ~jvm_balloon_api_impl();
    virtual void return_heap(size_t mem);
    virtual void adjust_memory(size_t threshold);
    virtual void voluntary_return();
    virtual bool fault(balloon_ptr b, exception_frame *ef, mmu::jvm_balloon_vma *vma);
    virtual bool ballooning();
private:
    jvm_balloon_shrinker *_balloon_shrinker;
};
}
#endif
