#include <osv/run.hh>
#include <osv/debug.hh>
#include <osv/condvar.h>
#include <osv/osv_execve.h>
#include <osv/wait_record.hh>
#include <thread>
#include <string>

/* Record thread state changes (termination) by storing exit status into a map.
 * It is used to implement waitpid like functionality for threads (osv_waittid).
 **/
class application_status {
public:
    application_status() {
        exit_code = 0;
        errno_code = 0;
    };
public:
    int exit_code;
    int errno_code;
};
static std::unordered_map<long, application_status> exited_threads;
static mutex exec_mutex;
static condvar cond;
extern "C" long gettid(); // implemented in linux.cc

#define debugf_execve(...) /* debugf(__VA_ARGS__) */

static int thread_run_app_in_namespace(std::string filename,
                                    const std::vector<std::string> args,
                                    const std::unordered_map<std::string, std::string> envp,
                                    long* thread_id,
                                    int notification_fd,
                                    waiter* parent_waiter)
{
    const bool new_program = true; // run in new ELF namespace
    long tid = gettid(); // sched::thread::current()->id();
    application_status app_status;

    debugf_execve("thread_run_app_in_namespace... tid=%ld\n", tid);
    assert(thread_id != nullptr);
    assert(parent_waiter != nullptr);
    *thread_id = tid;

    try {
        auto app = osv::application::run_and_join(filename, args, new_program, &envp, parent_waiter);
        app_status.exit_code = app->get_return_code();
        debugf_execve("thread_run_app_in_namespace ret = %d tid=%ld\n", app_status.exit_code, tid);
    } catch (osv::invalid_elf_error &ex) {
        app_status.errno_code = ENOEXEC;
        app_status.exit_code = -1;
    } catch (osv::launch_error &ex) {
        app_status.errno_code = ENOENT; // Assume all the other errors are "no such file"
        app_status.exit_code = -1;
    }

    WITH_LOCK(exec_mutex) {
        exited_threads[tid] = app_status;
        cond.wake_all();
    }

    if (app_status.errno_code != 0) {
        // osv::application::run_and_join failed, and likely didn't wake up parent_waiter.
        // Wake parent now, after errno is stored into exited_threads
        parent_waiter->wake();
    }

    // Trigger event notification via file descriptor (fd created with eventfd).
    if (notification_fd > 0) {
        uint64_t notif = 1;
        write(notification_fd, &notif, sizeof(notif));
    }
    return app_status.exit_code;
}

static std::vector<std::string> argv_to_array(const char *const argv[])
{
    std::vector<std::string> args;
    for (auto cur_arg = argv; cur_arg != nullptr && *cur_arg != nullptr && **cur_arg != '\0'; cur_arg++ ) {
        args.push_back(*cur_arg);
    }
    return args;
}

static std::unordered_map<std::string, std::string> envp_to_map(char *const envp[])
{
    char * const *env_kv;
    std::unordered_map<std::string, std::string> envp_map;
    for (env_kv = envp; env_kv != nullptr && *env_kv != nullptr && **env_kv != '\0'; env_kv++ ) {
        std::string key, value;
        std::string key_value(*env_kv);
        auto equal_pos = key_value.find('=');
        if (equal_pos != std::string::npos) {
            key = key_value.substr(0, equal_pos);
            value = key_value.substr(equal_pos + 1, key_value.length() - equal_pos - 1);
        }
        if (value == "") {
            fprintf(stderr, "ENVIRON ignoring ill-formated variable %s (not key=value)\n", key.c_str());
            continue;
        }
        envp_map[key] = value;
    }
    return envp_map;
}

extern "C" {

/*
 * Run filename in new thread, with its own memory (ELF namespace).
 * New thread ID is returned in thread_id.
 * On thread termination, event is triggered on notification_fd (if
 * notification_fd > 0).
 *
 * osv_waittid can be used on returned *thread_id to wait until new thread
 * terminates.
 * If notification_fd is used, osv_waittid can wait on arbitrary thread and
 * return thread_id of (first) finished thread.
 *
 * On sucess, 0 is returned. In addition, errno is cleared to 0.
 * On failure, -1 is returned and errno is set.
 **/
int osv_execve(const char *path, char *const argv[], char *const envp[],
    long *thread_id, int notification_fd)
{
    // will start app at path in new OSv thread, without replacing current binary.
    debugf_execve("OSv osv_execve:%d path=%s argv=%p envp=%p thread_id=%p %d notification_fd=%d\n",
        __LINE__, path, argv, envp, thread_id, thread_id? *thread_id: -1, notification_fd);
    debugf_execve("OSv osv_execve:%d   argv[0]=%p %s\n", __LINE__, argv, argv? argv[0]:NULL);
    debugf_execve("OSv osv_execve:%d   envp[0]=%p %s\n", __LINE__, envp, envp? envp[0]:NULL);

    // input thread_id might be NULL
    long tid, *ptid;
    ptid = thread_id? thread_id: &tid;
    *ptid = 0;

    /*
     * We have to start new program in new thread, otherwise current thread
     * waits until new program finishes.
     *
     * Caller might change memory at path, argv and envp, before new thread
     * has chance to use/copy the data. The std::thread constructor does make a
     * copy of data. That helps with say std::string (but would not with plain
     * char*).
     **/
    std::string filename(path);
    std::vector<std::string> args = argv_to_array(argv);
    std::unordered_map<std::string, std::string> envp_map = envp_to_map(envp);

    waiter w(sched::thread::current());
    // If need to set thread_id, wait until the newly created thread sets it
    // and also sets the new app_runtime on this thread.
    std::thread th = std::thread(thread_run_app_in_namespace,
        std::move(filename), std::move(args), std::move(envp_map),
        ptid, notification_fd, &w);
    // detach from thread so that no join needes to be called.
    th.detach();
    w.wait();
    // errno_code is set if app failed to start.
    WITH_LOCK(exec_mutex) {
        auto it = exited_threads.find(*ptid);
        if (it != exited_threads.end()) {
            auto app_status = it->second;
            errno = app_status.errno_code;
            return -1;
        }
    }

    // seems busybox expects execve to clear errno on success
    errno = 0;
    return 0;
}

/**
 * Implement waitpid-like functionality
 * @return - thread id which terminated, or 0 if nothing happened.
 * @tid - thread id we are interested into.
 *   0 or less means any thread id (started via osv_execve).
 *   osv_execve returns tid in thread_id.
 *   Note: this works only for threads started via osv_execve.
 * @status - called program/thread exit code will be stored there if non-NULL,
 *   in bits 15:8.
 * @options - if WNOHANG bit set, don't block.
 *
 **/
long osv_waittid(long tid, int *status, int options) {

    debugf_execve("osv_waittid tid=%ld options=%d (WNOHANG=%d) th_status_size=%d\n",
        tid, options, WNOHANG, exited_threads.size());

    /* Only here are elements removed from exited_threads. But we could be
     * called from two threads in parallel.
     */
    while (true) {
        WITH_LOCK(exec_mutex) {
            if (exited_threads.size() == 0 && options & WNOHANG) {
                // Nothing interesting so far, return immediately if WNOHANG is set.
                return 0;
            }

            while (exited_threads.size() == 0) {
                cond.wait(exec_mutex);
            }
            // some thread terminated, we might be interested in it.
            application_status app_status;
            long tid2;
            auto it = exited_threads.end();
            if (tid <= 0) {
                it = exited_threads.begin();
            }
            else {
                it = exited_threads.find(tid);
            }
            if (it != exited_threads.end()) {
                tid2 = it->first;
                app_status = it->second;
                debugf_execve("osv_waittid tid=%ld app.exit_code=%d app.errno_code=%d\n", tid2, app_status.exit_code, app_status.errno_code);
                if (status) {
                    *status = ((app_status.exit_code<<8) & 0x0000FF00);
                }
                exited_threads.erase(it);
                return tid2;
            }
            if (options & WNOHANG) {
                // Some thread did terminate, exited_threads.size() is > 0.
                // But we are not interested into that thread, so we must return.
                return 0;
            }
        }

        /* If some thread terminated, but no one is waiting on it,
         * exited_threads.size() will remain > 0. And this will spin.
         **/
    }
}

};
