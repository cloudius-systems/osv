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
#include <boost/signals2.hpp>

namespace osv {

class application;
using shared_app_t = std::shared_ptr<application>;

class launch_error : public std::runtime_error
{
public:
    launch_error(std::string msg) : std::runtime_error(msg) {}
};

/**
 * Represents an executing program.
 *
 */
class application : public std::enable_shared_from_this<application> {
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

    // Must be destroyed before _lib
    boost::signals2::signal<void()> _termination_signal;
private:
    void start();
    void main();
    void run_main(std::string path, int argc, char** argv);
    void run_main();
public:
    static shared_app_t get_current();

    /**
     * Start a new application.
     * args[0] should specify the command.
     *
     * \param args Parameters which will be passed to program's main().
     * \throw launch_error
     */
    static shared_app_t run(const std::vector<std::string>& args);

    ~application();

    /**
     * Start a new application.
     *
     * \param command command to execute
     * \param args Parameters which will be passed to program's main().
     * \throw launch_error
     */
    static shared_app_t run(const std::string& command, const std::vector<std::string>& args);

    application(const std::string& command, const std::vector<std::string>& args);

    /**
     * Waits until application terminates.
     *
     * @return application's exit code.
     */
    int join();

    /**
     * Moves current thread under this application context
     * Each thread can belong only to one application. If current
     * thread is already associated with some applicaiton it is
     * first removed from it.
     */
    void adopt_current();

    /**
     * Moves given thread under this application context.
     * Should be called only from that thread's constructor
     */
    void adopt(sched::thread* thread);

    /**
     * Removes current thread from this application context.
     */
    void abandon_current();

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
     * Returns application's return code. May only be called after join() returns.
     */
    int get_return_code();

    std::string get_command();
};

}

#endif /* _KERNEL */

namespace osv {

namespace this_application {

void on_termination_request(std::function<void()> callback);

}

}

#endif
