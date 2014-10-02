/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef JNI_HELPERS_HH_
#define JNI_HELPERS_HH_

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <jni.h>

#include "osv/rwlock.h"

/**
 * Helper types for JNI code. Encapsulating local/global refs,
 * classes and memeber queries.
 */


/**
 * RAII type to acquire the active JVM.
 * Will enforce the JVM to live across
 * the objects life time.
 */
class jvm_getter {
public:
    jvm_getter();

    JavaVM * jvm() const {
       return _jvm;
    }
    operator JavaVM *() const {
        return _jvm;
    }

    static void set_jvm(JavaVM *);
    static bool is_jvm_running() {
        return global_jvm != nullptr;
    }
private:
    std::lock_guard<rwlock_for_read>
        _guard;
    JavaVM * _jvm;

    static JavaVM * global_jvm;
    static rwlock global_lock;
};

/**
 * RAII type to query or attach the current
 * thread as a Java thread. Will deal with
 * recursive usage, as well as already attached
 * threads.
 */
class attached_env : public jvm_getter{
public:
    attached_env();
    ~attached_env();

    static attached_env & current();

    operator JNIEnv *() const {
        return env();
    }
    JNIEnv * env() const {
        return _env;
    }
private:
    JNIEnv * _env;
    bool     _attached = false;

    static __thread attached_env * tls_env;
};

/**
 * Scope level Java pending exception
 * check.
 *
 * This one is slightly weird. It tests
 * for a pending exception the destructor,
 * which will, if one is pending, in turn throw
 * a C++ exception.
 * But, you say, C++ objects may not throw in the destructor?
 * Yes, in fact they may. You just need to be very careful about it.
 * Had this class had any data members needing destruction, it would be
 * bad, but throwing an exception from this types destructor will in fact
 * not disrupt any other object being destroyed.
 *
 * See http://akrzemi1.wordpress.com/2011/09/21/destructors-that-throw/
 */
class jexception_check {
public:
    jexception_check();
    jexception_check(JNIEnv * env);
    ~jexception_check() noexcept(false);
private:
    JNIEnv * _env;
};

// base type for wrapper objects
template<typename T>
struct jwrapper {
public:
    operator T() const {
        return value();
    }
    operator bool() const {
        return _value != T();
    }
    T value() const {
        return _value;
    }
protected:
    jwrapper(T && t = T())
        : _value(t)
    {}
    jwrapper(const jwrapper<T> &) = default;

    T _value;
};

// Wrapper type for field/method IDs
template<typename ID>
class id_wrapper : public jwrapper<ID> {
public:
    typedef ID type;

    id_wrapper() = default;
    id_wrapper(jclass clz, const std::string & name, const std::string & sig, bool is_static = false)
        : id_wrapper(attached_env::current(), clz, name, sig, is_static)
    {}
    id_wrapper(JNIEnv * env, jclass clz, const std::string & name, const std::string & sig, bool is_static = false);
};

template<>
id_wrapper<jfieldID>::id_wrapper(JNIEnv * env, jclass clz, const std::string & name, const std::string & sig, bool is_static);

template<>
id_wrapper<jmethodID>::id_wrapper(JNIEnv * env, jclass clz, const std::string & name, const std::string & sig, bool is_static);

extern template class id_wrapper<jfieldID>;
extern template class id_wrapper<jmethodID>;

typedef id_wrapper<jfieldID> fieldID;
typedef id_wrapper<jmethodID> methodID;

// Java Global ref. I.e. reference survival scope beyond the active native frame scope.
template<typename T>
class jglobal : public jwrapper<T> {
public:
    jglobal() = default;
    jglobal(T t) : jglobal(attached_env::current(), t)
    {}
    jglobal(JNIEnv * env, T t) : jwrapper<T>(T(env->NewGlobalRef(t)))
    {}
    jglobal(jglobal<T> && t) : jwrapper<T>(t.value())
    {
        t._value = nullptr;
    }

    void set(T t) {
        this->_value = t != nullptr ? (T)attached_env::current().env()->NewGlobalRef(t) : nullptr;
    }

    jglobal<T> & operator=(T t) {
        set(t);
        return *this;
    }
    jglobal<T> & operator=(jglobal<T> && t) {
        this->_value = t._value;
        t._value = nullptr;
        return *this;
    }
};

/**
 * Local ref wrapper type that does actual class lookup.
 */
class jclazz : public jwrapper<jclass> {
public:

    jclazz() = default;
    jclazz(JNIEnv * env, const std::string & name, jobject loader = nullptr);
    jclazz(const std::string & name, jobject loader = nullptr)
    : jclazz(attached_env::current(), name, loader)
      {}
};

// Convert java string -> std::string (utf8)
class from_jstring : public std::string {
public:
    from_jstring(JNIEnv *, jstring);
    from_jstring(jstring s) : from_jstring(attached_env::current(), s)
    {}
};

// Convert std::string (utf8) -> java string
class to_jstring : public jwrapper<jstring> {
public:
    to_jstring(JNIEnv *, const std::string &);
    to_jstring(const std::string & s) : to_jstring(attached_env::current(), s)
    {}
};

#endif /* JNI_HELPERS_HH_ */
