/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef JAVA_API_HH_
#define JAVA_API_HH_

#include <string>
#include <vector>
#include <mutex>

struct JavaVM_;
class _jarray;
class jvm_error_exception : public std::exception
{
public:
    jvm_error_exception(const std::string& msg)
        : _msg(msg)
    {
    }
    virtual const char* what() const throw ()
    {
        return _msg.c_str();
    }

private:
    std::string _msg;

};

struct gc_info {
    long count;
    long time;
    std::string name;
    std::vector<std::string> pools;
};

/**
 * This is an entry point for the JVM that the API can use
 */
class java_api {
public:
    java_api()
        : jvm(nullptr)
    {
    }

    /**
     * get an mbean information
     * @param jmx_path an mbean name
     * @return a string representation of the mbean informtion
     */
    std::string get_mbean_info(const std::string& jmx_path);

    /**
     * Set an attribute in an mbean
     * @param jmx_path an mbean name
     * @param attribute the attribute to change
     * @param value the new value
     */
    void set_mbean_info(const std::string& jmx_path,
                        const std::string& attribute, const std::string& value);

    /**
     * Get a list of all the available mbean names
     * @return a vector of all the mbean names
     */
    std::vector<std::string> get_all_mbean();

    /**
     * Get a system property
     * @param property a property name
     * @return the system property value
     */
    std::string get_system_property(const std::string& property);

    /**
     * Get all the garbage collection information.
     * @return a vector with all the garbage collection information
     */
    std::vector<gc_info> get_all_gc();

    /**
     * Check if the jvm was set.
     * @return true if it was set or false otherwise
     */
    bool is_valid();

    /**
     * Explicitly call the garbage collection
     */
    void call_gc();

    /**
     * Set a jvm.
     * This method is called when a jvm goes up.
     * It calls the instance method internally
     * @param jvm the jvm to set
     */
    static void set(JavaVM_* jvm);

    /**
     * get an instance
     * @return a java_api instance
     */
    static java_api& instance();

private:
    /**
     * this is a safe method to get the jvm.
     * it make sure that the jvm is valid before returning it and
     * throw an exception if not.
     * @return the jvm.
     */
    JavaVM_& get_jvm()
    {
        if (!is_valid()) {
            throw jvm_error_exception("JVM not provided");
        }
        return *jvm;
    }

    static std::mutex lock;
    static java_api* _instance;
    JavaVM_* jvm;
};

#endif /* JAVA_API_HH_ */
