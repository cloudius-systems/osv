/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_APP_HH
#define _OSV_APP_HH

#include <memory>
#include <vector>
#include <osv/sched.hh>
#include <pthread.h>

namespace osv {

class application;
using shared_app_t = std::shared_ptr<application>;

/**
 * Represents an executing program.
 *
 */
class application {
private:
    pthread_t _thread;
    std::vector<std::string> _args;
    std::string _command;
    int _return_code;
private:
    void start();
    void main();
public:
    /**
     * Start a new application.
     * args[0] should specify the command.
     *
     * \param args Parameters which will be passed to program's main().
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

    application(const std::vector<std::string>& args);
    application(const std::string& command, const std::vector<std::string>& args);

    /**
     * Waits until application terminates.
     *
     * @return application's exit code.
     */
    int join();
};

}

#endif
