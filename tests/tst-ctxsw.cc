
#include <functional>
#include <memory>
#include <string>
#include <pthread.h>
#include <sys/time.h>
#include <cinttypes>
#include <stdio.h>
#include <stdint.h>

uint64_t target_time = 10; // seconds

#ifdef __OSV__

#include <sched.hh>

class pinned_thread {
public:
    explicit pinned_thread(std::function<void ()> f);
    void pin(unsigned cpu);
    void start();
    void join();
private:
    std::function<void ()> _f;
    sched::thread::attr _attr;
    std::unique_ptr<sched::thread> _thread;
};

pinned_thread::pinned_thread(std::function<void ()> f)
    : _f(f)
{
}

void pinned_thread::pin(unsigned cpu)
{
    _attr.pinned_cpu = sched::cpus[cpu];
}

void pinned_thread::start()
{
    _thread.reset(new sched::thread(_f, _attr));
    _thread->start();
}

void pinned_thread::join()
{
    _thread->join();
}

#else

#include <thread>
#include <sched.h>

class pinned_thread {
public:
    explicit pinned_thread(std::function<void ()> f);
    void pin(unsigned cpu);
    void start();
    void join();
private:
    void do_pin();
private:
    std::function<void ()> _f;
    bool _is_pinned = false;
    unsigned _cpu;
    std::unique_ptr<std::thread> _thread;
};

pinned_thread::pinned_thread(std::function<void ()> f)
    : _f(f)
{
}

void pinned_thread::pin(unsigned cpu)
{
    _is_pinned = true;
    _cpu = cpu;
}

void pinned_thread::start()
{
    _thread.reset(new std::thread([=] { do_pin(); _f(); }));
}

void pinned_thread::do_pin()
{
    if (_is_pinned) {
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(_cpu, &cs);
        sched_setaffinity(0, sizeof(cs), &cs);
    }
}

void pinned_thread::join()
{
    _thread->join();
}

#endif

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t mgmt_cond = PTHREAD_COND_INITIALIZER;
unsigned owner;
unsigned remain;

bool exiting;
unsigned nr_running;

void run_once(unsigned me)
{
    bool done = false;
    while (!done) {
        pthread_mutex_lock(&mtx);
        while (owner != me) {
            pthread_cond_wait(&cond, &mtx);
        }
        if (remain == 0) {
            done = true;
            pthread_cond_broadcast(&mgmt_cond);
        } else {
            --remain;
        }
        owner = !me;
        pthread_mutex_unlock(&mtx);
        pthread_cond_signal(&cond);
    }
}

void run(unsigned me)
{
    while (!exiting) {
        pthread_mutex_lock(&mtx);
        while (!exiting && !nr_running) {
            pthread_cond_wait(&mgmt_cond, &mtx);
        }
        if (exiting) {
            pthread_mutex_unlock(&mtx);
            break;
        }
        --nr_running;
        pthread_mutex_unlock(&mtx);
        run_once(me);
    }
}

uint64_t nstime()
{
    timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * uint64_t(1000000000) + tv.tv_usec * uint64_t(1000);
}

void test(std::string name,
        std::function<void (pinned_thread& t)> pin0,
        std::function<void (pinned_thread& t)> pin1)
{
    pinned_thread t0([] { run(0); }), t1([] { run(1); });
    pin0(t0);
    pin1(t1);
    auto n_iterations = 10000;
    exiting = false;
    bool converged = false;
    uint64_t start, end;

    t0.start();
    t1.start();

    while (!converged) {
        n_iterations *= 2;
        start = nstime();

        pthread_mutex_lock(&mtx);
        nr_running = 2;
        remain = n_iterations;
        pthread_cond_broadcast(&mgmt_cond);

        while (remain) {
            pthread_cond_wait(&mgmt_cond, &mtx);
        }

        pthread_mutex_unlock(&mtx);

        end = nstime();
        converged = (end - start) >= (target_time * 1000000000);
    }


    pthread_mutex_lock(&mtx);
    exiting = true;
    pthread_cond_broadcast(&mgmt_cond);
    pthread_mutex_unlock(&mtx);

    t0.join();
    t1.join();

    printf("%10" PRIu64 " %s\n", (end - start) / n_iterations, name.c_str());
}

int main(int ac, char** av)
{
    auto pin0 = [](pinned_thread& t) { t.pin(0); };
    auto pin1 = [](pinned_thread& t) { t.pin(1); };
    auto nopin = [](pinned_thread& t) {};
    test("colocated", pin0, pin0);
    test("apart", pin0, pin1);
    test("nopin", nopin, nopin);
}



