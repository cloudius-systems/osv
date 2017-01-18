
#include <osv/osv_c_wrappers.h>
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/app.hh>

using namespace osv;
using namespace sched;

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
