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
#include <thread>

// Java uses this global variable (supplied by Glibc) to figure out
// aproximatively where the initial thread's stack end.
// The aproximation allow to fill the variable here instead of doing it in
// osv::run.
void *__libc_stack_end;

namespace osv {

static thread_local shared_app_t current_app;

shared_app_t application::get_current()
{
    return current_app;
}

void application::adopt_current()
{
    if (current_app) {
        current_app->abandon_current();
    }
    current_app = shared_from_this();
}

void application::adopt(sched::thread* thread)
{
    auto& app_ref = thread->remote_thread_local_var(current_app);
    assert(!app_ref);
    app_ref = shared_from_this();
}

void application::abandon_current()
{
    current_app.reset();
}

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
    , _termination_requested(false)
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
    adopt_current();

    __libc_stack_end = __builtin_frame_address(0);

    sched::thread::current()->set_name(_command);

    _lib = osv::run(_command, _args, &_return_code);
    if (_lib) {
        // success
        if (_return_code) {
            debug("program %s returned %d\n", _command.c_str(), _return_code);
        }
        return;
    }

    printf("run_main(): cannot execute %s. Powering off.\n", _command.c_str());
    osv::poweroff();
}

void application::on_termination_request(std::function<void()> callback)
{
    auto app = current_app;
    std::unique_lock<mutex> lock(app->_termination_mutex);
    if (app->_termination_requested) {
        lock.unlock();
        callback();
        return;
    }

    app->_termination_signal.connect(callback);
}

void application::request_termination()
{
    WITH_LOCK(_termination_mutex) {
        if (_termination_requested) {
            return;
        }
        _termination_requested = true;
    }

    if (current_app.get() == this) {
        _termination_signal();
    } else {
        std::thread terminator([&] {
            adopt_current();
            _termination_signal();
        });
        terminator.join();
    }
}

namespace this_application {

void on_termination_request(std::function<void()> callback)
{
    application::on_termination_request(callback);
}

}

}
