/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "java_api.hh"
#include <jni.h>

using namespace std;
java_api* java_api::_instance = nullptr;
std::mutex java_api::lock;

/**
 * jvm_getter is a helper class to handle the JNI interaction.
 * It's constructor and destructor handle the attach and detach thread
 * and it holds helper method to handle JNI parsing.
 */
class jvm_getter {
public:
    /**
     * The constructor attach to the current thread.
     * @param _jvm a pointer to the jvm
     */
    jvm_getter(JavaVM& _jvm)
        : env(nullptr), jvm(_jvm)
    {
        if (jvm.AttachCurrentThread((void **) &env, nullptr) != 0) {
            throw jvm_error_exception("Fail attaching to jvm thread");
        }
    }

    /**
     * The destructor detach from the current thread
     */
    ~jvm_getter()
    {
        jvm.DetachCurrentThread();
    }

    /**
     * A helper method that check if an exception occur
     * if so, it throw a C++ exception
     */
    void check_exception()
    {
        jthrowable exc = env->ExceptionOccurred();
        if (exc) {
            env->ExceptionDescribe();
            jclass exClass = env->GetObjectClass(exc);
            jmethodID mid = env->GetMethodID(exClass, "toString",
                                             "()Ljava/lang/String;");

            env->ExceptionClear();
            jstring err_msg = (jstring) env->CallObjectMethod(exc, mid);
            throw jvm_error_exception(to_str(err_msg));
        }
    }
    /**
     * A helper method that return a java class
     * @param name the name of the class
     * @return the java class
     */
    jclass get_class(const string& name)
    {
        auto cls = env->FindClass(name.c_str());
        check_exception();
        if (cls == nullptr) {
            throw jvm_error_exception("Fail getting class " + name);
        }
        return cls;
    }

    /**
     * get the JavaInfo class
     * @return the JavaInfo
     */
    jclass get_java_info()
    {
        return get_class("io/osv/JavaInfo");
    }

    /**
     * A helper method to get a string  field from an array of java objects
     * @param arr an array of objects
     * @param pos the position in the array
     * @param name the name of the field.
     * @return a string with the value
     */
    string get_str_arr(jobjectArray arr, int pos, const string& name)
    {
        jobject obj = env->GetObjectArrayElement(arr, pos);
        jstring str = (jstring) env->GetObjectField(obj,
                      get_field_id(obj, name, "Ljava/lang/String;"));
        check_exception();
        return to_str(str);
    }

    /**
     * A helper method to get a long field from an array of java objects
     * @param arr an array of objects
     * @param pos the position in the array
     * @param name the name of the field.
     * @return the field value
     */
    long get_long_arr(jobjectArray arr, int pos, const string& name)
    {
        jobject obj = env->GetObjectArrayElement(arr, pos);
        jlong val = env->GetLongField(obj, get_field_id(obj, name, "J"));
        check_exception();
        return (long) val;
    }

    /**
     * A helper method to get java field id
     * @param obj the java object
     * @param field the field name
     * @param type the field type
     * @return the field id
     */
    jfieldID get_field_id(jobject obj, const string& field, const string& type)
    {
        jclass cls = env->GetObjectClass(obj);
        return env->GetFieldID(cls, field.c_str(), type.c_str());
    }

    /**
     * Create a C++ string from java string
     * @param str java string
     * @return a C++ string with the java string
     */
    string to_str(jstring str)
    {
        const char* tmp = env->GetStringUTFChars(str, NULL);
        string res = tmp;
        env->ReleaseStringUTFChars(str, tmp);
        return res;
    }

    JNIEnv* env;
    JavaVM& jvm;
};

bool java_api::is_valid()
{
    lock.lock();
    bool valid = instance().jvm != nullptr;
    lock.unlock();
    return valid;
}

void java_api::set(JavaVM_* jvm)
{
    lock.lock();
    instance().jvm = jvm;
    lock.unlock();
}

java_api& java_api::instance()
{
    lock.lock();
    if (_instance == nullptr) {
        _instance = new java_api();
    }
    lock.unlock();
    return *_instance;
}

std::string java_api::get_mbean_info(const std::string& jmx_path)
{
    jvm_getter info(get_jvm());

    auto java_info = info.get_java_info();

    jmethodID get_mbean = info.env->GetStaticMethodID(java_info,
                          "getMbean",
                          "(Ljava/lang/String;)Ljava/lang/String;");

    auto path = info.env->NewStringUTF(jmx_path.c_str());
    jstring str = (jstring) info.env->CallStaticObjectMethod(
                      java_info,
                      get_mbean, path);
    info.check_exception();
    return (str == nullptr) ? "" : info.to_str(str);
}

std::vector<std::string> java_api::get_all_mbean()
{
    jvm_getter info(get_jvm());

    auto java_info = info.get_java_info();

    jmethodID get_mbean = info.env->GetStaticMethodID(java_info,
                          "getAllMbean",
                          "()[Ljava/lang/String;");

    jobjectArray stringArray = (jobjectArray) info.env->CallStaticObjectMethod(
                                   java_info,
                                   get_mbean);
    info.check_exception();
    vector<string> res;
    for (int i = 0; i < info.env->GetArrayLength(stringArray); i++) {
        res.push_back(
            info.to_str(
                (jstring) info.env->GetObjectArrayElement(stringArray,
                        i)));
    }
    return res;
}

std::vector<gc_info> java_api::get_all_gc()
{
    jvm_getter info(get_jvm());

    auto java_info = info.get_java_info();
    jmethodID get_mbean = info.env->GetStaticMethodID(java_info,
                          "getAllGC",
                          "()[Lio/osv/GCInfo;");
    jobjectArray objArray = (jobjectArray) info.env->CallStaticObjectMethod(
                                java_info,
                                get_mbean);
    info.check_exception();
    std::vector<gc_info> res;
    for (int i = 0; i < info.env->GetArrayLength(objArray); i++) {
        gc_info gc;
        gc.name = info.get_str_arr(objArray, i, "name");
        gc.count = info.get_long_arr(objArray, i, "count");
        gc.time = info.get_long_arr(objArray, i, "time");
        res.push_back(gc);
    }
    return res;
}

std::string
java_api::get_system_property(const std::string& property)
{
    jvm_getter info(get_jvm());

    auto java_info = info.get_java_info();

    jmethodID get_mbean = info.env->GetStaticMethodID(java_info,
                          "getProperty",
                          "(Ljava/lang/String;)Ljava/lang/String;");

    auto str = info.env->NewStringUTF(property.c_str());
    jstring res = (jstring) info.env->CallStaticObjectMethod(java_info,
                  get_mbean, str);
    info.check_exception();
    return info.to_str(res);
}

void java_api::set_mbean_info(const std::string& jmx_path,
                              const std::string& attribute, const std::string& value)
{
    jvm_getter info(get_jvm());

    auto java_info = info.get_java_info();

    jmethodID set_mbean = info.env->GetStaticMethodID(java_info,
                          "setMbean",
                          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    auto str = info.env->NewStringUTF(jmx_path.c_str());
    auto attr = info.env->NewStringUTF(attribute.c_str());
    auto val = info.env->NewStringUTF(value.c_str());
    info.env->CallStaticVoidMethod(java_info,
                                   set_mbean, str, attr, val);
    info.check_exception();
}

void java_api::call_gc()
{
    jvm_getter info(get_jvm());
    auto system = info.get_class("java/lang/System");
    jmethodID gc = info.env->GetStaticMethodID(system,
                   "gc",
                   "()V");
    info.env->CallStaticVoidMethod(system, gc);
    info.check_exception();
}
