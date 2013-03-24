// test the fpu, especially with preemption enabled

#include "sched.hh"
#include <vector>
#include <atomic>
#include "debug.hh"
#include <boost/format.hpp>
#include <cmath>

bool test()
{
    constexpr unsigned nr_angles = 100, repeats = 100000;
    double sins[nr_angles];
    bool bad = false;
    for (unsigned i = 0; i < nr_angles; ++i) {
        sins[i] = std::sin(double(i));
    }
    for (unsigned j = 0; j < repeats; ++j) {
        for (unsigned i = 0; i < nr_angles; ++i) {
            double v1 = std::sin(double(i));
            double v2 = sins[i];
            bad |= v1 != v2;
            if (bad) {
                while (true) ;
            }
        }
    }
    debug(boost::format("3 -> %f") % sins[3]);
    return !bad;
}

typedef boost::format fmt;

extern "C" {
    int osv_main(int ac, char **av);
}

int main(int ac, char **av)
{
    constexpr unsigned nr_threads = 16;
    std::vector<sched::thread*> threads;

    debug("starting fpu test");
    std::atomic<int> tests{}, fails{};
    for (unsigned i = 0; i < nr_threads; ++i) {
        auto t = new sched::thread([&] {
            if (!test()) {
                ++fails;
            }
            ++tests;
        });
        threads.push_back(t);
        t->start();
    }
    for (auto t : threads) {
        t->join();
        delete t;
    }
    debug(fmt("fpu test done, %d/%d fails/tests") % fails % tests);
    return !fails ? 0 : 1;
}
