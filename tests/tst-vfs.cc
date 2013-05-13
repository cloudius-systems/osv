#include "sched.hh"
#include "debug.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    debug("Running concurrent file operation tests\n");

    constexpr int N = 10;
    debug("test1, with %d threads\n", N);
    sched::thread *threads[N];
    for (int i = 0; i < N; i++) {
            threads[i] = new sched::thread([] {
                    struct stat buf;
                    for (int j = 0; j < 1000; j++) {
                        assert(stat("/usr/lib/jvm/jre/lib/amd64/headless/libmawt.so", &buf)==0);
                    }
            });
    }
    for (int i=0; i<N; i++) {
        threads[i]->start();
    }
    for (int i=0; i<N; i++) {
        threads[i]->join();
        delete threads[i];
    }

    debug("concurrent file operation tests succeeded\n");
    return 0;
}
