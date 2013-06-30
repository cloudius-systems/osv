//
// Instructions: run this test with 4 vcpus
//
#include <cstdlib>
#include <ctime>
#include <sched.hh>
#include <debug.hh>
#include <lockfree/ring.hh>

//
// Create 2 threads on different CPUs which perform concurrent push/pop
// Testing spsc ring
//
class test_spsc_push_pop {
public:

    static const int max_random = 25;

    bool run()
    {
        assert (sched::cpus.size() >= 2);

        sched::thread * thread1 = new sched::thread([&] { thread_push(0); },
            sched::thread::attr(sched::cpus[0]));
        sched::thread * thread2 = new sched::thread([&] { thread_pop(1); },
            sched::thread::attr(sched::cpus[1]));

        thread1->start();
        thread2->start();

        thread1->join();
        thread2->join();

        delete thread1;
        delete thread2;

        bool success = true;
        debug("Results:\n");
        for (int i=0; i < max_random; i++) {
            unsigned pushed = _stats[0][i];
            unsigned popped = _stats[1][i];

            debug("    value=%-08d pushed=%-08d popped=%-08d\n", i,
                pushed, popped);

            if (pushed != popped) {
                success = false;
            }
        }

        return success;
    }

private:

    ring_spsc<int, 4096> _ring;
    const u64 elements_to_process = 1000000000;

    int _stats[2][max_random] = {};

    void thread_push(int cpu_id)
    {
        std::srand(std::time(0));
        for (u64 ctr=0; ctr < elements_to_process; ctr++)
        {
            int element = std::rand() % max_random;
            while (!_ring.push(element));
            _stats[cpu_id][element]++;
        }
    }

    void thread_pop(int cpu_id)
    {
        std::srand(std::time(0));
        for (u64 ctr=0; ctr < elements_to_process; ctr++)
        {
            int element = 0;
            while (!_ring.pop(element));
            _stats[cpu_id][element]++;
        }
    }
};

int main(int argc, char **argv)
{
    test_spsc_push_pop t1;
    bool rc = t1.run();

    return rc ? 0 : 1;
}
