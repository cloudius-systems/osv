/*
 * Copyright (C) 2022 Waldemar Kozaczuk
 * Copyright (C) 2016 XLAB, d.o.o.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/osv_c_wrappers.h>
#include <osv/export.h>
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/app.hh>
#include <osv/run.hh>
#include <osv/version.hh>
#include <osv/commands.hh>
#include <osv/firmware.hh>
#include <osv/hypervisor.hh>
#include "cpuid.hh"
#include <vector>

using namespace osv;
using namespace sched;

extern "C" OSV_MODULE_API
int osv_get_all_app_threads(pid_t tid, pid_t** tid_arr, size_t *len) {
    thread* app_thread = tid==0? thread::current(): thread::find_by_id(tid);
    if (app_thread == nullptr) {
        return ESRCH;
    }
    std::vector<thread*> app_threads;
    with_all_app_threads([&](thread& th2) {
        app_threads.push_back(&th2);
    }, *app_thread);

    *tid_arr = (pid_t*)malloc(app_threads.size()*sizeof(pid_t));
    if (*tid_arr == nullptr) {
        *len = 0;
        return ENOMEM;
    }
    *len = 0;
    for (auto th : app_threads) {
        (*tid_arr)[(*len)++] = th->id();
    }
    return 0;
}

static void free_threads_names(std::vector<osv_thread> &threads) {
    for (auto &t : threads) {
        if (t.name) {
            free(t.name);
        }
    }
}

static char* str_to_c_str(const std::string& str) {
    auto len = str.size();
    char *buf = static_cast<char*>(malloc(len + 1)); // This will be free()-ed in C world
    if (buf) {
        std::copy(str.begin(), str.end(), buf);
        buf[len] = '\0';
    }
    return buf;
}

extern "C" OSV_MODULE_API
int osv_get_all_threads(osv_thread** thread_arr, size_t *len) {
    using namespace std::chrono;
    std::vector<osv_thread> threads;

    osv_thread thread;
    bool str_copy_error = false;
    sched::with_all_threads([&](sched::thread &t) {
        thread.id = t.id();
        auto tcpu = t.tcpu();
        thread.cpu_id = tcpu ? tcpu->id : -1;
        thread.cpu_ms = duration_cast<milliseconds>(t.thread_clock()).count();
        thread.switches = t.stat_switches.get();
        thread.migrations = t.stat_migrations.get();
        thread.preemptions = t.stat_preemptions.get();
        thread.name = str_to_c_str(t.name());
        if (!thread.name) {
            str_copy_error = true;
        }
        thread.priority = t.priority();
        thread.stack_size = t.get_stack_info().size;
        thread.status = static_cast<osv_thread_status>(static_cast<int>(t.get_status()));
        threads.push_back(thread);
    });

    if (str_copy_error) {
        goto error;
    }

    *thread_arr = (osv_thread*)malloc(threads.size()*sizeof(osv_thread));
    if (*thread_arr == nullptr) {
        goto error;
    }

    std::copy(threads.begin(), threads.end(), *thread_arr);
    *len = threads.size();
    return 0;

error:
    free_threads_names(threads);
    *len = 0;
    return ENOMEM;
}

extern "C" OSV_MODULE_API
char *osv_version() {
    return str_to_c_str(osv::version());
}

extern "C" OSV_MODULE_API
char *osv_cmdline() {
    return str_to_c_str(osv::getcmdline());
}

extern "C" OSV_MODULE_API
char *osv_hypervisor_name() {
    return str_to_c_str(osv::hypervisor_name());
}

extern "C" OSV_MODULE_API
char *osv_firmware_vendor() {
    return str_to_c_str(osv::firmware_vendor());
}

extern "C" OSV_MODULE_API
char *osv_processor_features() {
    return str_to_c_str(processor::features_str());
}

extern char debug_buffer[DEBUG_BUFFER_SIZE];
extern "C" OSV_MODULE_API
const char *osv_debug_buffer() {
    return debug_buffer;
}

extern "C" OSV_MODULE_API
void osv_current_app_on_termination_request(void (*handler)()) {
    osv::this_application::on_termination_request(handler);
}

extern bool verbose;
extern "C" OSV_MODULE_API
bool osv_debug_enabled() {
    return verbose;
}
