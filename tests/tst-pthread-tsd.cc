#include <atomic>
#include <cstdio>
#include <cstdlib>

#include <pthread.h>
#include <unistd.h>

std::atomic<unsigned> tests_total(0), tests_failed(0);
std::atomic<unsigned> constructions(0), destructions(0), destructions_recur(0);

void report(const char* name, bool passed)
{
    static const char* status[] = { "FAIL", "PASS" };
    printf("%s: %s\n", status[passed], name);
    tests_total += 1;
    tests_failed += !passed;
}

const unsigned num_threads = 10;
pthread_key_t key;

void dtor_recur(void *data)
{
   destructions_recur++;
}

class test_class {
public:
    test_class(int v) : val(v) { constructions++; }
    ~test_class() {
       destructions++;

       pthread_key_t key_recur;
       int ret = pthread_key_create(&key_recur, dtor_recur);
       report("pthread_key_create (key_recur)", ret == 0);

       ret = pthread_setspecific(key_recur, reinterpret_cast<void *>(42));
       report("pthread_setspecific (key_recur)", ret == 0);
    }
    int val;
};

void dtor(void *data)
{
    delete static_cast<test_class *>(data);
}

void *secondary(void *arg)
{
    long val = reinterpret_cast<long>(arg);
    auto data = new test_class(val);

    int ret = pthread_setspecific(key, data);
    report("pthread_setspecific", ret == 0);

    usleep(10);

    data = static_cast<test_class *>(pthread_getspecific(key));
    report("pthread_getspecific", data->val == val);

    return NULL;
}

int main()
{
    int ret;

    ret = pthread_key_create(&key, dtor);
    report("pthread_key_create", ret == 0);

    pthread_t threads[num_threads];

    for (unsigned i = 0; i < num_threads; ++i) {
        ret = pthread_create(&threads[i], NULL, secondary, reinterpret_cast<void *>(i));
        report("pthread_create", ret == 0);
    }

    for (auto &thread : threads) {
        ret = pthread_join(thread, NULL);
        report("pthread_join", ret == 0);
    }

    report("10 constructions", constructions == 10);
    report("10 destructions", destructions == 10);
    report("10 recursive destructions", destructions_recur == 10);

    printf("SUMMARY: %u tests / %u failures\n", tests_total.load(), tests_failed.load());
    return tests_failed == 0 ? 0 : 1;
}
