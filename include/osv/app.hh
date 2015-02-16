/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_APP_HH
#define _OSV_APP_HH

#include <functional>

#ifdef _KERNEL

#include <memory>
#include <vector>
#include <osv/sched.hh>
#include <pthread.h>
#include <osv/mutex.h>
#include <osv/elf.hh>
#include <list>

extern "C" void __libc_start_main(int(*)(int, char**), int, char**, void(*)(),
    void(*)(), void(*)(), void*);

namespace osv {

class application;
using shared_app_t = std::shared_ptr<application>;

extern __thread application* override_current_app;

class launch_error : public std::runtime_error
{
public:
    launch_error(std::string msg) : std::runtime_error(msg) {}
};

class multiple_join_error : public std::runtime_error
{
public:
    multiple_join_error() : std::runtime_error("Join was called more than once") {}
};

class app_registry {
public:
    void join();
    void push(shared_app_t app);
    bool remove(application* app);
private:
    std::list<shared_app_t> apps;
    mutex lock;
};

struct application_runtime {
    application_runtime(application& app) : app(app) { }
    ~application_runtime();

    application& app;
};

/**
 * Represents an executing program.
 *
 */
class application : public std::enable_shared_from_this<application> {
public:
    /**
     * Returns application of the current thread.
     */
    static shared_app_t get_current();

    /**
     * Start a new application.
     * args[0] should specify the command to run.
     *
     * \param args Arguments passed to the program's main() function.
     * \throw launch_error
     */
    static shared_app_t run(const std::vector<std::string>& args);

    /**
     * Start a new application.
     *
     * \param command command to execute
     * \param args Parameters which will be passed to program's main().
     * \throw launch_error
     */
    static shared_app_t run(const std::string& command, const std::vector<std::string>& args);

    static void join_all() {
        apps.join();
    }

    application(const std::string& command, const std::vector<std::string>& args);

    ~application();

    /**
     * Waits until application terminates.
     *
     * @return application's exit code.
     */
    int join();

    /**
     * Installs a termination callback which will be called when
     * termination is requested or immediately if termination was
     * already requested.
     *
     * The callback is called in application's context.
     */
    static void on_termination_request(std::function<void()> callback);

    /**
     * Will call termination request callbacks. Can be called many times but
     * only the first call has any effect.
     *
     * Can be called from a thread which is not this application's thread.
     */
    void request_termination();

    /**
     * "stop" all threads attached to this application except this one, and
     * remove them from this application's context. Running threads are not
     * touched (if there is one, false is returned), and other threads are
     * moved to a state that will never run again. This operation is slow and
     * generally unsafe because the stopped threads could be holding resources
     * such as internal OSv mutexes, which will never be released.
     * Only call this method if you are sure that the remaining threads
     * are not in the middle of I/O, memory allocation, or anything else which
     * might be holding critical OSv resources.
     */
    static bool unsafe_stop_and_abandon_other_threads();

    /**
     * Returns application's return code. May only be called after join() returns.
     */
    int get_return_code();

    /**
     * Returns the invoked program executable of this application.
     */
    std::string get_command();

    std::shared_ptr<application_runtime> runtime() const { return _runtime; }
private:
    shared_app_t get_shared() {
        return shared_from_this();
    }
    void start();
    void main();
    void run_main(std::string path, int argc, char** argv);
    void run_main();
    friend void ::__libc_start_main(int(*)(int, char**), int, char**, void(*)(),
        void(*)(), void(*)(), void*);

private:
    using main_func_t = int(int, char**);

    pthread_t _thread;
    std::vector<std::string> _args;
    std::string _command;
    int _return_code;
    bool _termination_requested;
    mutex _termination_mutex;
    std::shared_ptr<elf::object> _lib;
    main_func_t* _main;
    void (*_entry_point)();
    static app_registry apps;

    // Must be destroyed before _lib, because contained function objects may
    // have destructors which are part of the application's code.
    std::list<std::function<void()>> _termination_request_callbacks;

    std::shared_ptr<application_runtime> _runtime;
    sched::thread* _joiner;
    std::atomic<bool> _terminated;
    friend struct application_runtime;
};

}

#endif /* _KERNEL */

namespace osv {
/**
 * Creates a new app
 * args Arguments passed to the program's main() function.
 */
void run(const std::vector<std::string>& args);

namespace this_application {

void on_termination_request(std::function<void()> callback);

}

}

#endif
