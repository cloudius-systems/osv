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
#include <osv/trace.hh>
#include <functional>
#include <thread>
#include <libgen.h>
#include <errno.h>
#include <algorithm>
#include <boost/range/algorithm/transform.hpp>

using namespace boost::range;

extern int optind;

// Java uses this global variable (supplied by Glibc) to figure out
// aproximatively where the initial thread's stack end.
void *__libc_stack_end;

extern "C" void __libc_start_main(int (*main)(int, char**), int, char**,
    void(*)(), void(*)(), void(*)(), void*)
{
    auto app = osv::application::get_current();
    assert(app->_entry_point);
    app->_main = main;
    app->run_main();
    app.reset();
    pthread_exit(nullptr);
}

namespace osv {

__thread application* override_current_app;

void app_registry::join() {
    while (true) {
        shared_app_t p;
        WITH_LOCK(lock) {
            if (apps.empty()) {
                return;
            }
            p = apps.front();
        }
        try {
            p->join();
        } catch (const multiple_join_error& e) {
            // At the clean up stage even if join was called before
            // it will be ignore allowing to complete the join
        }
    }
}

bool app_registry::remove(application* app) {
    bool found = false;
    WITH_LOCK(lock) {
        apps.remove_if([app, &found](shared_app_t a){
            if (a.get() == app) {
                found = true;
                return true;
            }
            return false;
        });
    }
    return found;
}

void app_registry::push(shared_app_t app) {
    WITH_LOCK(lock) {
        apps.push_back(app);
    }
}

application_runtime::~application_runtime()
{
    if (app._joiner) {
        app._joiner->wake_with([&] { app._terminated.store(true); });
    }
}

app_registry application::apps;

shared_app_t application::get_current()
{
    auto runtime = sched::thread::current()->app_runtime();
    if (!runtime) {
        return nullptr;
    }
    return runtime->app.get_shared();
}

bool application::unsafe_stop_and_abandon_other_threads()
{
    auto current = sched::thread::current();
    auto current_runtime = current->app_runtime();
    bool success = true;
    // We don't have a list of threads belonging to this app, so need to do
    // it the long slow way, listing all threads...
    sched::with_all_threads([&](sched::thread &t) {
        if (&t != current && t.app_runtime() == current_runtime) {
            if (t.unsafe_stop()) {
                t.set_app_runtime(nullptr);
            } else {
                success = false;
            }
        }
    });
    return success;
}

shared_app_t application::run(const std::vector<std::string>& args)
{
    return run(args[0], args);
}

shared_app_t application::run(const std::string& command, const std::vector<std::string>& args)
{
    auto app = std::make_shared<application>(command, args);
    app->start();
    apps.push(app);
    return app;
}

void run(const std::vector<std::string>& args) {
    application::run(args);
}

application::application(const std::string& command, const std::vector<std::string>& args)
    : _args(args)
    , _command(command)
    , _termination_requested(false)
    , _runtime(new application_runtime(*this))
    , _joiner(nullptr)
    , _terminated(false)
{
    try {
        _lib = elf::get_program()->get_library(_command);
    } catch(const std::exception &e) {
        throw launch_error(e.what());
    }

    if (!_lib) {
        throw launch_error("Failed to load object: " + command);
    }

    _main = _lib->lookup<int (int, char**)>("main");
    if (!_main) {
        _entry_point = reinterpret_cast<void(*)()>(_lib->entry_point());
    }
    if (!_entry_point && !_main) {
        throw launch_error("Failed looking up main");
    }
}

void application::start()
{
    // FIXME: we cannot create the thread inside the constructor because
    // the thread would attempt to call shared_from_this() before object
    // is constructed which is illegal.
    override_current_app = this;
    auto err = pthread_create(&_thread, NULL, [](void *app) -> void* {
        ((application*)app)->main();
        return nullptr;
    }, this);
    override_current_app = nullptr;
    if (err) {
        throw launch_error("Failed to create the main thread, err=" + std::to_string(err));
    }
}

TRACEPOINT(trace_app_destroy, "app=%p", application*);

application::~application()
{
    assert(_runtime.use_count() == 1 || !_runtime);
    trace_app_destroy(this);
}

TRACEPOINT(trace_app_join, "app=%p", application*);
TRACEPOINT(trace_app_join_ret, "return_code=%d", int);

int application::join()
{
    if (!apps.remove(this)) {
        throw multiple_join_error();
    }
    trace_app_join(this);
    auto err = pthread_join(_thread, NULL);
    assert(!err);

    _joiner = sched::thread::current();
    _runtime.reset();
    sched::thread::wait_until([&] { return _terminated.load(); });

    _termination_request_callbacks.clear();
    _lib.reset();

    trace_app_join_ret(_return_code);
    return _return_code;
}

TRACEPOINT(trace_app_main, "app=%p, cmd=%s", application*, const char*);
TRACEPOINT(trace_app_main_ret, "return_code=%d", int);

void application::main()
{
    __libc_stack_end = __builtin_frame_address(0);

    sched::thread::current()->set_name(_command);

    if (_main) {
        run_main();
    } else {
        // The application is expected not to initialize the environment in
        // which it runs on its owns but to call __libc_start_main(). If that's
        // not the case bad things may happen: constructors of global objects
        // may be called twice, TLS may be overriden and the program may not
        // received correct arguments, environment variables and auxiliary
        // vector.
        _entry_point();
    }

    // _entry_point() doesn't return
}

void application::run_main(std::string path, int argc, char** argv)
{
    char *c_path = (char *)(path.c_str());
    // path is guaranteed to keep existing this function
    program_invocation_name = c_path;
    program_invocation_short_name = basename(c_path);

    auto sz = argc; // for the trailing 0's.
    for (int i = 0; i < argc; ++i) {
        sz += strlen(argv[i]);
    }

    std::unique_ptr<char []> argv_buf(new char[sz]);
    char *ab = argv_buf.get();
    char *contig_argv[argc + 1];

    for (int i = 0; i < argc; ++i) {
        size_t asize = strlen(argv[i]);
        memcpy(ab, argv[i], asize);
        ab[asize] = '\0';
        contig_argv[i] = ab;
        ab += asize + 1;
    }
    contig_argv[argc] = nullptr;

    // make sure to have a fresh optind across calls
    // FIXME: fails if run() is executed in parallel
    int old_optind = optind;
    optind = 0;
    _return_code = _main(argc, contig_argv);
    optind = old_optind;
}

void application::run_main()
{
    trace_app_main(this, _command.c_str());

    // C main wants mutable arguments, so we have can't use strings directly
    std::vector<std::vector<char>> mut_args;
    transform(_args, back_inserter(mut_args),
            [](std::string s) { return std::vector<char>(s.data(), s.data() + s.size() + 1); });
    std::vector<char*> argv;
    transform(mut_args.begin(), mut_args.end(), back_inserter(argv),
            [](std::vector<char>& s) { return s.data(); });
    auto argc = argv.size();
    argv.push_back(nullptr);
    run_main(_command, argc, argv.data());

    if (_return_code) {
        debug("program %s returned %d\n", _command.c_str(), _return_code);
    }

    trace_app_main_ret(_return_code);
}

TRACEPOINT(trace_app_termination_callback_added, "app=%p", application*);
TRACEPOINT(trace_app_termination_callback_fired, "app=%p", application*);

void application::on_termination_request(std::function<void()> callback)
{
    auto app = get_current();
    std::unique_lock<mutex> lock(app->_termination_mutex);
    if (app->_termination_requested) {
        lock.unlock();
        callback();
        return;
    }

    app->_termination_request_callbacks.push_front(std::move(callback));
}

TRACEPOINT(trace_app_request_termination, "app=%p, requested=%d", application*, bool);
TRACEPOINT(trace_app_request_termination_ret, "");

void application::request_termination()
{
    WITH_LOCK(_termination_mutex) {
        trace_app_request_termination(this, _termination_requested);
        if (_termination_requested) {
            trace_app_request_termination_ret();
            return;
        }
        _termination_requested = true;
    }

    if (get_current().get() == this) {
        for (auto &callback : _termination_request_callbacks) {
            callback();
        }
    } else {
        override_current_app = this;
        std::thread terminator([&] {
            for (auto &callback : _termination_request_callbacks) {
                callback();
            }
        });
        override_current_app = nullptr;
        terminator.join();
    }

    trace_app_request_termination_ret();
}

int application::get_return_code()
{
    return _return_code;
}

std::string application::get_command()
{
    return _command;
}

namespace this_application {

void on_termination_request(std::function<void()> callback)
{
    application::on_termination_request(callback);
}

}

}
