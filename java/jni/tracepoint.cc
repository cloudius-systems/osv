#include "tracepoint.hh"
#include <osv/trace.hh>
#include <osv/per-cpu-counter.hh>
#include <debug.hh>

static std::string get_string(JNIEnv* jni, jstring s)
{
    auto p = jni->GetStringUTFChars(s, nullptr);
    std::string ret(p);
    jni->ReleaseStringUTFChars(s, p);
    return ret;
}

JNIEXPORT jlongArray JNICALL Java_com_cloudius_trace_Tracepoint_doList
  (JNIEnv *jni, jclass klass)
{
    auto nr = tracepoint_base::tp_list.size();
    auto a = jni->NewLongArray(nr);
    size_t idx = 0;
    for (auto& tp : tracepoint_base::tp_list) {
        jlong handle = jlong(reinterpret_cast<uintptr_t>(&tp));
        jni->SetLongArrayRegion(a, idx++, 1, &handle);
    };
    return a;
}

JNIEXPORT jlong JNICALL Java_com_cloudius_trace_Tracepoint_findByName
  (JNIEnv *jni, jclass klass, jstring name)
{
    auto n = get_string(jni, name);
    for (auto& tp : tracepoint_base::tp_list) {
        if (n == tp.name) {
            return reinterpret_cast<uintptr_t>(&tp);
        }
    }
    auto re = jni->FindClass("java/lang/RuntimeException");
    jni->ThrowNew(re, "Cannot find tracepoint");
    return 0;
}

JNIEXPORT void JNICALL Java_com_cloudius_trace_Tracepoint_doEnable
  (JNIEnv *jni, jclass klass, jlong handle)
{
    auto tp = reinterpret_cast<tracepoint_base*>(handle);
    tp->enable();
}

JNIEXPORT jstring JNICALL Java_com_cloudius_trace_Tracepoint_doGetName
  (JNIEnv *jni, jclass klass, jlong handle)
{
    auto tp = reinterpret_cast<tracepoint_base*>(handle);
    return jni->NewStringUTF(tp->name);
}

class tracepoint_counter : public tracepoint_base::probe {
public:
    explicit tracepoint_counter(tracepoint_base& tp) : _tp(tp) {
        _tp.add_probe(this);
    }
    virtual ~tracepoint_counter() { _tp.del_probe(this); }
    virtual void hit() { _counter.increment(); }
    ulong read() { return _counter.read(); }
private:
    tracepoint_base& _tp;
    per_cpu_counter _counter;
};

JNIEXPORT jlong JNICALL Java_com_cloudius_trace_Tracepoint_doCreateCounter
  (JNIEnv *jni, jclass klass, jlong handle)
{
    auto tp = reinterpret_cast<tracepoint_base*>(handle);
    auto c = new tracepoint_counter(*tp);
    return reinterpret_cast<jlong>(c);
}

JNIEXPORT void JNICALL Java_com_cloudius_trace_Tracepoint_destroyCounter
  (JNIEnv *jni, jclass klazz, jlong handle)
{
    auto c = reinterpret_cast<tracepoint_counter*>(handle);
    delete c;
}

JNIEXPORT jlong JNICALL Java_com_cloudius_trace_Tracepoint_readCounter
  (JNIEnv *jni, jclass klass, jlong handle)
{
    auto c = reinterpret_cast<tracepoint_counter*>(handle);
    return c->read();
}

