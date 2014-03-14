#include <stdio.h>
#include <sys/mman.h>
#include <jni.h>
#include <api/assert.h>
#include <osv/align.hh>
#include <exceptions.hh>
#include <memcpy_decode.hh>
#include <boost/intrusive/set.hpp>
#include <osv/trace.hh>
#include "jvm_balloon.hh"
#include <osv/debug.hh>
#include <osv/mmu.hh>
#include <unordered_map>

TRACEPOINT(trace_jvm_balloon_fault, "from=%p, to=%p, size %d, vma_size %d",
        const unsigned char *, const unsigned char *, size_t, size_t);
TRACEPOINT(trace_jvm_balloon_move, "new_jvm_addr=%p, new_jvm_end=%p, new_addr %p, new_end %p",
        const unsigned char *, const unsigned char *, const unsigned char *, const unsigned char *);


class balloon {
public:
    explicit balloon(unsigned char *jvm_addr, jobject jref, int alignment, size_t size);

    void release(JNIEnv *env);

    ulong empty_area(balloon_ptr b);
    size_t size() { return _balloon_size; }
    size_t move_balloon(balloon_ptr b, mmu::jvm_balloon_vma *vma, unsigned char *dest);
    unsigned char *candidate_addr(mmu::jvm_balloon_vma *vma, unsigned char *dest);
private:
    void conciliate(unsigned char *addr);
    unsigned char *_jvm_addr;

    jobject _jref;
    unsigned int _alignment;
    size_t _balloon_size = balloon_size;
};

mutex balloons_lock;
std::list<balloon_ptr> balloons;

// We will use the following two statistics to aid our decision about whether
// or not we should balloon. They allow us to make informed decisions about what
// is the memory usage figure since a given reference point.
//
// In the future, we may move these to common code (say, mempool.cc), and have
// the checkpoints be independent of whether or not a call to balloon has happened.
// That is particularly important if we have many other kinds of shrinking agents.
// But right now, let's keep it here for simplicity.
static std::atomic<size_t> last_freed_memory(0);
static std::atomic<size_t> last_jvm_heap_memory(0);

// allocated is the inverse of free
static ssize_t recent_allocated()
{
    auto curr = memory::stats::free();
    return last_freed_memory.exchange(curr) - curr;
}

static ssize_t recent_jvm_heap()
{
    auto curr = memory::stats::jvm_heap();
    return curr - last_jvm_heap_memory.exchange(curr);
}

ulong balloon::empty_area(balloon_ptr b)
{
    auto jvm_end_addr = _jvm_addr + _balloon_size;
    auto addr = align_up(_jvm_addr, _alignment);
    auto end = align_down(jvm_end_addr, _alignment);

    balloons.push_back(b);
    return mmu::map_jvm(_jvm_addr, end - addr, _alignment, b);
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
}

unsigned char *
balloon::candidate_addr(mmu::jvm_balloon_vma *vma, unsigned char *dest)
{
    size_t skipped = static_cast<unsigned char *>(vma->addr()) - vma->jvm_addr();
    return dest - skipped;
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
        if (_vm->AttachCurrentThread((void **) env, NULL) != 0) {
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

size_t jvm_balloon_shrinker::request_memory(size_t size)
{
    JNIEnv *env = NULL;
    size_t ret = 0;

    WITH_LOCK(balloons_lock) {
        ssize_t last_heap = recent_jvm_heap();
        ssize_t last_used = recent_allocated();

        // Beware: because we are just estimating used from two timepoints, it
        // can actually be 0.  For instance, if we allocated 100 Mb of JVM heap
        // and freed 100 Mb of file memory.
        if (last_used && ((last_heap * 100) / last_used  > 80)) {
            deactivate_shrinker();
            return 0;
        }
    }

    int status = _attach(&env);

    // It is unfortunate that we need to evaluate those every time, but the JNI
    // functions are associated with a particular env pointer. So if we reuse
    // any of those values, they will be invalid in the next invocation. The
    // whole thing takes around 30 ms though, so it should be fine.
    auto rtclass = env->FindClass("java/lang/Runtime");
    auto rt = env->GetStaticMethodID(rtclass , "getRuntime", "()Ljava/lang/Runtime;");
    auto rtinst = env->CallStaticObjectMethod(rtclass, rt);
    auto free_memory  = env->GetMethodID(rtclass, "freeMemory", "()J");

    do {
        size_t free = env->CallLongMethod(rtinst, free_memory);

        // Don't overstress the heap. If we have not enough heap size for a
        // balloon, we are unlikely to do any good.
        if ((free < balloon_size) || ((free * 100) / _total_heap) < 20) {
            deactivate_shrinker();
            break;
        }

        jbyteArray array = env->NewByteArray(balloon_size);
        jthrowable exc = env->ExceptionOccurred();
        if (exc) {
            env->ExceptionClear();
            deactivate_shrinker();
            break;
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
        // Avoid entering any endless loops. Fail imediately
        if (!iscopy)
            break;
    } while (ret < size);

    _detach(status);
    return ret;
}

size_t jvm_balloon_shrinker::release_memory(size_t size)
{
    JNIEnv *env = NULL;
    int status = _attach(&env);

    size_t ret = 0;
    WITH_LOCK(balloons_lock) {
        while ((ret < size) && !balloons.empty()) {
            auto b = balloons.back();

            balloons.pop_back();

            ret += b->size();
            b->release(env);
            // It might be that this shrinker was disabled due to excessive memory
            // pressure, so we must take care to activate it. This should be a nop
            // if the shrinker is already active, so do it always.
            activate_shrinker();
        }
    }

    _detach(status);
    return ret;
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
    if (!ef || (ef->error_code == mmu::page_fault_write)) {
        if (vma->effective_jvm_addr()) {
            return false;
        }
        delete vma;
        return true;
    }

    memcpy_decoder *decode = memcpy_find_decoder(ef);
    if (!decode) {
        if (vma->effective_jvm_addr()) {
            return false;
        }
        delete vma;
        return true;
    }

    unsigned char *dest = memcpy_decoder::dest(ef);
    unsigned char *src = memcpy_decoder::src(ef);
    size_t size = decode->size(ef);

    trace_jvm_balloon_fault(src, dest, size, vma->size());

    auto skip = size;
    if (size < vma->size()) {
        unsigned char *base = static_cast<unsigned char *>(vma->addr());
        // In case the copy does not start from the beginning of the balloon,
        // we calculate where should the begin be. We always want to move the
        // balloon in its entirety.
        auto offset = src - base;

        auto candidate = b->candidate_addr(vma, dest - offset);

        if ((src + size) > reinterpret_cast<unsigned char *>(vma->end())) {
            skip = (reinterpret_cast<unsigned char *>(vma->end()) - src);
        }

        if (vma->add_partial(skip, candidate)) {
            delete vma;
        }
    } else {
        // If the JVM is also copying objects that are laid after the balloon, we
        // need to copy only the bytes up until the end of the balloon. If the
        // copy is a partial copy in the middle of the object, then we should
        // drain the counter completely
        skip = b->move_balloon(b, vma, dest);
    }
    decode->memcpy_fixup(ef, std::min(skip, size));
    return true;
}

jvm_balloon_shrinker::jvm_balloon_shrinker(JavaVM_ *vm)
    : shrinker("jvm_shrinker")
    , _vm(vm)
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
    auto total_memory = env->GetMethodID(rtclass, "totalMemory", "()J");
    _total_heap = env->CallLongMethod(rtinst, total_memory);

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

    // Reset statistics
    recent_jvm_heap();
    recent_allocated();

    _detach(status);
}
