/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <jni.h>

#include <osv/mutex.h>
#include "jni_helpers.hh"

using namespace std;

JavaVM * jvm_getter::global_jvm;
rwlock jvm_getter::global_lock;

jvm_getter::jvm_getter()
    : _guard(global_lock.for_read())
    , _jvm(global_jvm)
{
    if (_jvm == nullptr) {
        throw std::runtime_error("No JVM running");
    }
}

void jvm_getter::set_jvm(JavaVM * vm) {
    WITH_LOCK(global_lock.for_write()) {
        global_jvm = vm;
    }
}

attached_env::attached_env()
    : _env(nullptr)
{
    if (tls_env == nullptr) {
        if (jvm()->GetEnv((void **)&_env, JNI_VERSION_1_6) != JNI_OK) {
            if (jvm()->AttachCurrentThread((void **) &_env, nullptr) != 0) {
                throw std::runtime_error("Fail attaching to jvm thread");
            }
            _attached = true;
        }
        tls_env = this;
    } else {
        _env = tls_env->_env;
    }
}

attached_env::~attached_env()
{
    if (tls_env == this) {
        tls_env = nullptr;
    }
    if (_attached && jvm() != nullptr) {
        jvm()->DetachCurrentThread();
    }
}

attached_env & attached_env::current() {
    if (!tls_env)  {
        throw std::runtime_error("No attached env");
    }
    return *tls_env;
}

jexception_check::jexception_check() : jexception_check(attached_env::current())
{}

jexception_check::jexception_check(JNIEnv * env) : _env(env)
{}

jexception_check::~jexception_check() noexcept(false)
{
    if (_env->ExceptionCheck())  {
        auto exc = _env->ExceptionOccurred();
        _env->ExceptionDescribe();
        _env->ExceptionClear();
        jclass exClass = _env->GetObjectClass(exc);
        jmethodID mid = _env->GetMethodID(exClass, "toString", "()Ljava/lang/String;");
        jstring err_msg = mid != nullptr ? (jstring) _env->CallObjectMethod(exc, mid) : nullptr;
        throw std::runtime_error(from_jstring(err_msg));
    }
}


template<>
id_wrapper<jfieldID>::id_wrapper(JNIEnv * env, jclass clz, const std::string & name, const std::string & sig, bool is_static)
    : jwrapper<type>([env, clz, &name, &sig, is_static]() -> jfieldID {
        jexception_check chk(env);
        return is_static ?
            env->GetStaticFieldID(clz, name.c_str(), sig.c_str()) :
            env->GetFieldID(clz, name.c_str(), sig.c_str());
    }())
{}

template<>
id_wrapper<jmethodID>::id_wrapper(JNIEnv * env, jclass clz, const std::string & name, const std::string & sig, bool is_static)
    : jwrapper<type>([env, clz, &name, &sig, is_static]() -> jmethodID {
        jexception_check chk(env);
        return is_static ?
                        env->GetStaticMethodID(clz, name.c_str(), sig.c_str()) :
                        env->GetMethodID(clz, name.c_str(), sig.c_str());
    }())
{}

template class id_wrapper<jfieldID>;
template class id_wrapper<jmethodID>;

jclazz::jclazz(JNIEnv * env, const std::string & name, jobject loader) {
    jexception_check chk(env);
    jclass clz;
    if (loader == nullptr) {
        clz = env->FindClass(name.c_str());
    } else {
        static jglobal<jclass> classLoader(jclazz("java/lang/ClassLoader"));
        static methodID loadClass(classLoader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        std::string tmp(name);
        std::replace(tmp.begin(), tmp.end(), '/', '.');
        clz = (jclass)env->CallObjectMethod(loader, loadClass, to_jstring(tmp));
    }
    _value = clz;
}

from_jstring::from_jstring(JNIEnv * env, jstring s)
{
    if (s != nullptr) {
        auto * utf = env->GetStringUTFChars(s, nullptr);
        assign(utf);
        env->ReleaseStringUTFChars(s, utf);
    }
}

to_jstring::to_jstring(JNIEnv * env, const std::string & s)
{
    jexception_check chk(env);
    _value = env->NewStringUTF(s.c_str());
}

__thread attached_env * attached_env::tls_env = nullptr;
