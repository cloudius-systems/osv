#include <stdio.h>
#include <sys/mman.h>
#include <jni.h>
#include <api/assert.h>
#include <osv/align.hh>
#include <exceptions.hh>

#ifndef AARCH64_PORT_STUB
#include <memcpy_decode.hh>
#endif /* !AARCH64_PORT_STUB */

#include <boost/intrusive/set.hpp>
#include <osv/trace.hh>
#include "jvm_balloon.hh"
#include <osv/debug.hh>
#include <osv/mmu.hh>
#include <unordered_map>
#include <thread>

TRACEPOINT(trace_jvm_balloon_new, "obj=%p, aligned %p, size %d, vma_size %d",
        const unsigned char *, const unsigned char *, size_t, size_t);
TRACEPOINT(trace_jvm_balloon_free, "");
TRACEPOINT(trace_jvm_balloon_fault, "from=%p, to=%p, size %d, vma_size %d",
        const unsigned char *, const unsigned char *, size_t, size_t);
TRACEPOINT(trace_jvm_balloon_move, "new_jvm_addr=%p, new_jvm_end=%p, new_addr %p, new_end %p",
        const unsigned char *, const unsigned char *, const unsigned char *, const unsigned char *);
TRACEPOINT(trace_jvm_balloon_close, "from=%p, to=%p, condition=%s",
        uintptr_t, uintptr_t, const char *);


jvm_balloon_shrinker *balloon_shrinker = nullptr;

namespace memory {

// If we are under pressure, we will end up setting the voluntary return flag
// many times. To avoid returning one balloon per call, let's use a boolean to
// control that. When the balloon we have given back is finally returned, we
// reset the process.
static std::atomic<bool> balloon_voluntary_return = { true };

static std::atomic<size_t> jvm_heap_allowance(0);
void reserve_jvm_heap(size_t mem)
{
    jvm_heap_allowance.fetch_sub(mem, std::memory_order_relaxed);
}

void return_jvm_heap(size_t mem)
{
    jvm_heap_allowance.fetch_add(mem, std::memory_order_relaxed);
    balloon_voluntary_return = true;
}

ssize_t jvm_heap_reserved()
{
    if (!balloon_shrinker) {
        return 0;
    }
    return (stats::free() + stats::jvm_heap()) - jvm_heap_allowance.load(std::memory_order_relaxed);
}

void jvm_balloon_adjust_memory(size_t threshold)
{
    if (!balloon_shrinker) {
        return;
    }

    // Core of the reservation system:
    // The heap allowance starts as the initial memory that is reserved to
    // the JVM. It means how much it can eventually use, and it is completely
    // dissociated with the amount of memory it is using now. When we balloon,
    // that number goes down, and when we return the balloon back, it goes
    // up again.
    if (jvm_heap_reserved() <= static_cast<ssize_t>(threshold)) {
        balloon_shrinker->request_memory(1);
    }
}

bool throttling_needed()
{
    if (!balloon_shrinker) {
        return false;
    }

    return balloon_shrinker->ballooning();
}
};

void jvm_balloon_voluntary_return()
{
    if (!balloon_shrinker) {
        return;
    }

    // If we freed memory and now we have more than a balloon + 20 % worth of
    // reserved memory, give it back to the Java Heap. This is because it is a
    // lot harder to react to JVM memory shortages than it is to react to OSv
    // memory shortages - which are effectively under our control. Don't doing
    // this can result in Heap exhaustions in situations where JVM allocation
    // rates are very high and memory is tight
    if ((memory::jvm_heap_reserved() > 6 * static_cast<ssize_t>(balloon_size/5)) &&
        memory::balloon_voluntary_return.exchange(false))
    {
        balloon_shrinker->release_memory(1);
    }
}

class balloon {
public:
    explicit balloon(unsigned char *jvm_addr, jobject jref, int alignment, size_t size);

    void release(JNIEnv *env);

    ulong empty_area(balloon_ptr b);
    size_t size() { return _balloon_size; }
    size_t move_balloon(balloon_ptr b, mmu::jvm_balloon_vma *vma, unsigned char *dest);
    unsigned char *candidate_addr(mmu::jvm_balloon_vma *vma, unsigned char *dest);
    // This is useful when communicating back the size of the area that is now
    // available for the OS.  The actual size can easily change. For instance,
    // if one allocation happens to be aligned, this will be the entire size of
    // the balloon. But that doesn't matter: In that case we will just return
    // the minimum size, and some pages will be unavailable.
    size_t minimum_size() { return _balloon_size - _alignment; }
private:
    void conciliate(unsigned char *addr);
    unsigned char *_jvm_addr;

    jobject _jref;
    unsigned int _alignment;
    size_t _balloon_size = balloon_size;
};

mutex balloons_lock;
std::list<balloon_ptr> balloons;

ulong balloon::empty_area(balloon_ptr b)
{
#ifndef AARCH64_PORT_STUB
    auto jvm_end_addr = _jvm_addr + _balloon_size;
    auto addr = align_up(_jvm_addr, _alignment);
    auto end = align_down(jvm_end_addr, _alignment);

    balloons.push_back(b);
    auto ret = mmu::map_jvm(_jvm_addr, end - addr, _alignment, b);
    memory::reserve_jvm_heap(minimum_size());
    trace_jvm_balloon_new(_jvm_addr, addr, end - addr, ret);
    return ret;
#else /* AARCH64_PORT_STUB */
    abort();
#endif /* AARCH64_PORT_STUB */
}

balloon::balloon(unsigned char *jvm_addr, jobject jref, int alignment = mmu::huge_page_size, size_t size = balloon_size)
    : _jvm_addr(jvm_addr), _jref(jref), _alignment(alignment), _balloon_size(size)
{
    assert(mutex_owned(&balloons_lock));
}

// Giving memory back to the JVM only means deleting the reference.  Without
// any pending references, the Garbage collector will be responsible for
// disposing the object when it really needs to. As for the OS memory, note
// that since we are operating in virtual addresses, we have to mmap the memory
// back. That does not guarantee that it will be backed by pages until the JVM
// decides to reuse it for something else.
void balloon::release(JNIEnv *env)
{
    assert(mutex_owned(&balloons_lock));

    // No need to remap. Will happen automatically when JVM touches it again
    env->DeleteGlobalRef(_jref);
    memory::return_jvm_heap(minimum_size());
    trace_jvm_balloon_free();
}

unsigned char *
balloon::candidate_addr(mmu::jvm_balloon_vma *vma, unsigned char *dest)
{
#ifndef AARCH64_PORT_STUB
    size_t skipped = static_cast<unsigned char *>(vma->addr()) - vma->jvm_addr();
    return dest - skipped;
#else  /* AARCH64_PORT_STUB */
    abort();
#endif /* AARCH64_PORT_STUB */
}

size_t balloon::move_balloon(balloon_ptr b, mmu::jvm_balloon_vma *vma, unsigned char *dest)
{
    // We need to calculate how many bytes we will skip if this were the new
    // balloon, but we won't touch the mappings yet. That will be done at conciliation
    // time when we're sure of it.
    auto candidate_jvm_addr = candidate_addr(vma, dest);
    auto candidate_jvm_end_addr = candidate_jvm_addr + _balloon_size;
    auto candidate_addr = align_up(candidate_jvm_addr, _alignment);
    auto candidate_end = align_down(candidate_jvm_end_addr, _alignment);

    trace_jvm_balloon_move(candidate_jvm_addr, candidate_jvm_end_addr, candidate_addr, candidate_end);
    mmu::map_jvm(candidate_jvm_addr, candidate_end - candidate_addr, _alignment, b);

    return candidate_jvm_end_addr - dest;
}

// We can either be called from a java thread, or from the shrinking code in OSv.
// In the first case we can just grab a pointer to env, but in the later we need
// to attach our C++ thread to the JVM. Only in that case we will need to detach
// later, so keep track through passing the status over as a handler.
int jvm_balloon_shrinker::_attach(JNIEnv **env)
{
    int status = _vm->GetEnv((void **)env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        // Need to span a daemon, otherwise we will block JVM exit.
        if (_vm->AttachCurrentThreadAsDaemon((void **) env, NULL) != 0) {
            assert(0);
        }
    } else {
        assert(status == JNI_OK);
    }
    return status;
}

void jvm_balloon_shrinker::_detach(int status)
{
    if (status != JNI_OK) {
        _vm->DetachCurrentThread();
    }
}

size_t jvm_balloon_shrinker::_request_memory(JNIEnv *env, size_t size)
{
    size_t ret = 0;

    do {
        jbyteArray array = env->NewByteArray(balloon_size);
        jthrowable exc = env->ExceptionOccurred();
        if (exc) {
            env->ExceptionClear();
            return -1;
        }

        jboolean iscopy=0;
        auto p = env->GetPrimitiveArrayCritical(array, &iscopy);

        // OpenJDK7 will always return false when we are using
        // GetPrimitiveArrayCritical, and true when we are using
        // GetPrimitiveArray.  Still, better test it since this is not mandated
        // by the interface. If we receive a copy of the array instead of the
        // actual array, this address is pointless.
        if (!iscopy) {
            // When calling JNI, all allocated objects will have local
            // references that prevent them from being garbage collected. But
            // those references will only prevent the object from being Garbage
            // Collected while we are executing JNI code. We need to acquire a
            // global reference.  Later on, we will invalidate it when we no
            // longer want this balloon to stay around.
            jobject jref = env->NewGlobalRef(array);
            WITH_LOCK(balloons_lock) {
                balloon_ptr b{new balloon(static_cast<unsigned char *>(p), jref)};
                ret += b->empty_area(b);
            }
        }
        env->ReleasePrimitiveArrayCritical(array, p, 0);
        env->DeleteLocalRef(array);
        // Avoid entering any endless loops. Fail imediately
        if (iscopy)
            break;
    } while (ret < size);

    return ret;
}

void jvm_balloon_shrinker::_release_memory(JNIEnv *env, size_t size)
{
    WITH_LOCK(balloons_lock) {
        auto b = balloons.back();


        balloons.pop_back();

        size -= b->size();
        b->release(env);
    }
}

extern "C" size_t arc_sized_adjust(int64_t to_reclaim);

void jvm_balloon_shrinker::_thread_loop()
{
    JNIEnv *env;
    int status = _attach(&env);
    _thread = sched::thread::current();
    _thread->set_name("JVMBalloon");
    _thread->set_priority(0.001);

    thread_mark_emergency();

    while (true) {
        WITH_LOCK(balloons_lock) {
            _blocked.wait_until(balloons_lock, [&] { return (_pending.load() + _pending_release.load()) > 0; });

            if (balloons.size() >= _soft_max_balloons) {
                memory::wake_reclaimer();
            }

            if (_pending_release.load() != 0) {
                int extra = balloons.size() - _soft_max_balloons;
                if (extra > 0) {
                    _pending_release.fetch_add(extra);
                }
            }

            while (_pending_release.load()) {
                if (balloons.empty()) {
                    _pending_release.store(0);
                    break;
                }
                size_t freed = 1;
                while ((memory::jvm_heap_reserved() < static_cast<ssize_t>(balloon_size)) && (freed != 0)) {
                    uint64_t to_free = balloon_size - memory::jvm_heap_reserved();
                    freed = arc_sized_adjust(to_free);
                }

                if (memory::jvm_heap_reserved() >= static_cast<ssize_t>(balloon_size)) {
                    _pending_release.fetch_sub(1);
                    _release_memory(env, memory::jvm_heap_reserved());
                } else {
                    _pending_release.store(0);
                }
            }

            while ((memory::jvm_heap_reserved() <= 0)) {
                _request_memory(env, 1);
            }
            _pending.store(0);
        }
    }
    _detach(status);
}

// We have created a byte array and evacuated its addresses. Java is not ever
// expected to touch the variable itself because no code does it. But when GC
// is called, it will move the array to a different location. Because the array
// is paged out, this will generate a fault. We can trap that fault and then
// manually resolve it.
//
// However, we need to be careful about one thing: The JVM will not move parts
// of the heap in an object-by-object basis, but rather copy large chunks at
// once. So there is no guarantee whatsoever about the kind of addresses we
// will receive here. Only that there is a balloon in the middle. So the best
// thing to do is to emulate the memcpy in its entirety, not only the balloon
// part.  That means copying the part that comes before the balloon, playing
// with the maps for the balloon itself, and then finish copying the part that
// comes after the balloon.
bool jvm_balloon_fault(balloon_ptr b, exception_frame *ef, mmu::jvm_balloon_vma *vma)
{
#ifndef AARCH64_PORT_STUB
    if (!ef || (ef->error_code == mmu::page_fault_write)) {
        if (vma->effective_jvm_addr()) {
            return false;
        }
        trace_jvm_balloon_close(vma->start(), vma->end(), "write");
        delete vma;
        return true;
    }

    memcpy_decoder *decode = memcpy_find_decoder(ef);
    if (!decode) {
        if (vma->effective_jvm_addr()) {
            return false;
        }
        delete vma;
        trace_jvm_balloon_close(vma->start(), vma->end(), "nodecoder");
        return true;
    }

    unsigned char *dest = memcpy_decoder::dest(ef);
    unsigned char *src = memcpy_decoder::src(ef);
    size_t size = decode->size(ef);

    trace_jvm_balloon_fault(src, dest, size, vma->size());

    auto skip = size;
    if (size < vma->real_size()) {
        unsigned char *base = static_cast<unsigned char *>(vma->addr());
        // In case the copy does not start from the beginning of the balloon,
        // we calculate where should the begin be. We always want to move the
        // balloon in its entirety.
        auto offset = src - base;

        auto candidate = b->candidate_addr(vma, dest - offset);

        // shortcut partial moves to self. If we are moving to ourselves, we
        // will eventually complete, there is no need to keep track. This is
        // not only an optimization, this is also a correctness issue. Because
        // of the move to self, some writes that fall into the same range may
        // have returned false in the closing tests.
        if (vma->addr() == align_up(candidate, balloon_alignment)) {
            decode->memcpy_fixup(ef, size);
            return true;
        }

        if ((src + size) > reinterpret_cast<unsigned char *>(vma->end())) {
            skip = (reinterpret_cast<unsigned char *>(vma->end()) - src);
        }

        if (vma->add_partial(skip, candidate)) {
            trace_jvm_balloon_close(vma->start(), vma->end(), "partialclose");
            delete vma;
        }
    } else {
        // For the partial size it can actually happen that we got lowered in size
        assert(vma->size() >= b->minimum_size());

        // If the JVM is also copying objects that are laid after the balloon, we
        // need to copy only the bytes up until the end of the balloon. If the
        // copy is a partial copy in the middle of the object, then we should
        // drain the counter completely
        skip = b->move_balloon(b, vma, dest);
    }
    decode->memcpy_fixup(ef, std::min(skip, size));
    return true;
#else /* AARCH64_PORT_STUB */
    abort();
#endif /* AARCH64_PORT_STUB */
}

jvm_balloon_shrinker::jvm_balloon_shrinker(JavaVM_ *vm)
    : _vm(vm)
{
    JNIEnv *env = NULL;
    int status = _attach(&env);

    auto monitor = env->FindClass("io/osv/OSvGCMonitor");
    if (!monitor) {
        debug("java.so: Can't find monitor class\n");
    }

    auto rtclass = env->FindClass("java/lang/Runtime");
    auto rt = env->GetStaticMethodID(rtclass , "getRuntime", "()Ljava/lang/Runtime;");
    auto rtinst = env->CallStaticObjectMethod(rtclass, rt);
    auto total_memory = env->GetMethodID(rtclass, "maxMemory", "()J");
    _total_heap = env->CallLongMethod(rtinst, total_memory);
    // As the name implies, this is a soft maximum. We are allowed to go over
    // it, but as soon as we need to give memory back to the JVM, we will go
    // down towards number. This is because if the JVM is very short on
    // memory, it can quickly fill up the new balloon and may not have time
    // for a new GC cycle.
    _soft_max_balloons = (_total_heap / ( 2 * balloon_size)) - 1;

    auto monmethod = env->GetStaticMethodID(monitor, "MonitorGC", "(J)V");
    env->CallStaticVoidMethod(monitor, monmethod, this);
    jthrowable exc = env->ExceptionOccurred();
    if (exc) {
        printf("WARNING: Could not start OSV Monitor, and JVM Balloon is being disabled.\n"
               "To fix this problem, please start OSv manually specifying the Heap Size.\n");
        env->ExceptionDescribe();
        env->ExceptionClear();
        abort();
    }

    _detach(status);

    balloon_shrinker = this;

    // This cannot be a sched::thread because it may call into JNI functions,
    // if the JVM balloon is registered as a shrinker. It expects the full pthread
    // API to be functional, and for sched::threads it is not.
    // std::thread is implemented ontop of pthreads, so it is fine
    std::thread tmp([=] { _thread_loop(); });
    tmp.detach();
}

jvm_balloon_shrinker::~jvm_balloon_shrinker()
{
    balloon_shrinker = nullptr;
}
