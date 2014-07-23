/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/app.hh>
#include <string>
#include <osv/run.hh>
#include <osv/power.hh>
#include <functional>

// Java uses this global variable (supplied by Glibc) to figure out
// aproximatively where the initial thread's stack end.
// The aproximation allow to fill the variable here instead of doing it in
// osv::run.
void *__libc_stack_end;

namespace osv {

shared_app_t application::run(const std::vector<std::string>& args)
{
    auto app = std::make_shared<application>(args);
    app->start();
    return app;
}

shared_app_t application::run(const std::string& command, const std::vector<std::string>& args)
{
    auto app = std::make_shared<application>(command, args);
    app->start();
    return app;
}

application::application(const std::vector<std::string>& args)
    : application(args[0], args)
{
}

application::application(const std::string& command, const std::vector<std::string>& args)
    : _args(args)
    , _command(command)
{
}

void application::start()
{
    // FIXME: we cannot create the thread inside the constructor because
    // the thread would attempt to call shared_from_this() before object
    // is constructed which is illegal.
    auto err = pthread_create(&_thread, NULL, [](void *app) -> void* {
        ((application*)app)->main();
        return nullptr;
    }, this);
    assert(!err);
}

int application::join()
{
    auto err = pthread_join(_thread, NULL);
    assert(!err);
    return _return_code;
}

void application::main()
{
    __libc_stack_end = __builtin_frame_address(0);

    sched::thread::current()->set_name(_command);

    auto lib = osv::run(_command, _args, &_return_code);
    if (lib) {
        // success
        if (_return_code) {
            debug("program %s returned %d\n", _command.c_str(), _return_code);
        }
        return;
    }

    printf("run_main(): cannot execute %s. Powering off.\n", _command.c_str());
    osv::poweroff();
}

}
