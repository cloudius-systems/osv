/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_APP_HH
#define _OSV_APP_HH

#include <functional>

#include <memory>
#include <vector>
#include <osv/sched.hh>
#include <pthread.h>
#include <osv/mutex.h>
#include <osv/elf.hh>
#include <list>
#include <unordered_map>
#include <string>

#include "musl/include/elf.h"
#undef AT_UID // prevent collisions
#undef AT_GID

extern "C" void __libc_start_main(int(*)(int, char**), int, char**, void(*)(),
    void(*)(), void(*)(), void*);

class waiter;

namespace osv {

class application;
using shared_app_t = std::shared_ptr<application>;

extern __thread application* override_current_app;

class launch_error : public std::runtime_error
{
public:
    launch_error(std::string msg) : std::runtime_error(msg) {}
};

class invalid_elf_error : public launch_error
{
public:
    invalid_elf_error(std::string msg) : launch_error(msg) {}
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
     * \param new_program true if a new elf namespace must be started
     * \param env pointer to an unordered_map than will be merged in current env
     * \throw launch_error
     */
    static shared_app_t run(const std::string& command,
            const std::vector<std::string>& args,
            bool new_program = false,
            const std::unordered_map<std::string, std::string> *env = nullptr);

    static void join_all() {
        apps.join();
    }

    application(const std::string& command,
            const std::vector<std::string>& args,
            bool new_program = false,
            const std::unordered_map<std::string, std::string> *env = nullptr);

    ~application();

    /**
     * Waits until application terminates.
     *
     * @return application's exit code.
     */
    int join();

    /**
     * Start a new application and wait for it to terminate.
     *
     * run_and_join() is like run() followed by join(), with one important
     * difference: because run() returns control to the caller, it needs
     * to run the program in a new thread. But run_and_join() waits for
     * the program to finish, so it can run it in the current thread,
     * without creating a new one.
     *
     * \param command command to execute
     * \param args Parameters which will be passed to program's main().
     * \param new_program true if a new elf namespace must be started
     * \param env pointer to an unordered_map than will be merged in current env
     * \throw launch_error
     */
    static shared_app_t run_and_join(const std::string& command,
            const std::vector<std::string>& args,
            bool new_program = false,
            const std::unordered_map<std::string, std::string> *env = nullptr,
            waiter* setup_waiter = nullptr);

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

    /**
      * Returns thread_id/PID of thread running app main() function.
      */
    pid_t get_main_thread_id();

    std::shared_ptr<application_runtime> runtime() const { return _runtime; }
    std::shared_ptr<elf::object> lib() const { return _lib; }

    elf::program *program();
private:
    void new_program();
    void clone_osv_environ();
    void set_environ(const std::string &key, const std::string &value,
                     bool new_program);
    void merge_in_environ(bool new_program = false,
        const std::unordered_map<std::string, std::string> *env = nullptr);
    shared_app_t get_shared() {
        return shared_from_this();
    }
    void start();
    void start_and_join(waiter* setup_waiter);
    void main();
    void run_main(std::string path, int argc, char** argv);
    void prepare_argv(elf::program *program);
    void run_main();
    friend void ::__libc_start_main(int(*)(int, char**), int, char**, void(*)(),
        void(*)(), void(*)(), void*);

private:
    using main_func_t = int(int, char**);

    pthread_t _thread;
    std::unique_ptr<elf::program> _program; // namespace program
    std::vector<std::string> _args;
    std::string _command;
    int _return_code;
    bool _termination_requested;
    mutex _termination_mutex;
    std::shared_ptr<elf::object> _lib;
    std::shared_ptr<elf::object> _libenviron;
    std::shared_ptr<elf::object> _libvdso;
    main_func_t* _main;
    void (*_entry_point)();
    static app_registry apps;

    // _argv is set by prepare_argv() called from the constructor and needs to be
    // retained as member variable so that it later can be passed as argument by either
    // application::main and application::run_main() or application::run_main() called
    // from __libc_start_main()
    std::unique_ptr<char *[]> _argv;
    std::unique_ptr<char []> _argv_buf; // actual arguments content _argv points to

    // Must be destroyed before _lib, because contained function objects may
    // have destructors which are part of the application's code.
    std::list<std::function<void()>> _termination_request_callbacks;

    std::shared_ptr<application_runtime> _runtime;
    sched::thread* _joiner;
    std::atomic<bool> _terminated;
    friend struct application_runtime;
};

/*
Execute f on all threads which belong to same app as t1 does.
*/
void with_all_app_threads(std::function<void(sched::thread &)> f, sched::thread& th1);

}

namespace osv {

namespace this_application {

void on_termination_request(std::function<void()> callback);

}

}

#endif
