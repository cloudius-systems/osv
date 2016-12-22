#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <limits.h>
#include <stdlib.h>
#include <atomic>

unsigned int tests_total = 0, tests_failed = 0;

void report(const char* name, bool passed)
{
    static const char* status[] = {"FAIL", "PASS"};
    printf("%s: %s\n", status[passed], name);
    tests_total += 1;
    tests_failed += !passed;
}

// Opaque type 32 bytes in size
static pthread_barrier_t barrier;
// Opaque type 4 bytes in size
pthread_barrierattr_t attr;
// Number of crossings across the barrier
static int numCrossings;
// Counter to track the number of special return values to threads
static std::atomic<int> specialRetVals;

static void* thread_func(void *arg)
{
    int threadNum = arg ? *((int*) arg): 0;
    int retval = 0;
    printf("[Thread %d] starting...\n", threadNum);

    for (int crossing = 0; crossing < numCrossings; crossing++) {
        // Force threads to sleep for a random interval so we randomize
        // which thread might get the special return value
        // PTHREAD_BARRIER_SERIAL_THREAD
        int delay = random() % 7 + 1;
        usleep(delay);

        printf("[Thread %d] waiting on barrier\n", threadNum);
        retval = pthread_barrier_wait(&barrier);
        if (retval == PTHREAD_BARRIER_SERIAL_THREAD) {
            printf("[Thread %d] crossed barrier with %d\n", threadNum,
                   PTHREAD_BARRIER_SERIAL_THREAD);
            // Increment the counter of special return values
            specialRetVals.fetch_add(1, std::memory_order_relaxed);
        } else if (retval == 0) {
            printf("[Thread %d] crossed barrier with %d\n", threadNum, retval);
        }
    }
    return 0;
}

int main(void)
{
    // Number of threads that must call into the barrier before they all unblock
    const int numThreads = 10;
    // Pass through the barrier k times
    numCrossings = 4;
    specialRetVals.store(0, std::memory_order_relaxed);
    pthread_t threads[numThreads];
    int threadIds[numThreads];
    int retval = -1;
    printf("Sizeof pthread_barrier_t    : %ld\n", sizeof(barrier));
    report("sizeof pthread_barrier_t is 32 bytes\n", sizeof(barrier) == 32);
    printf("Sizeof pthread_barrierattr_t: %ld\n", sizeof(attr));
    report("sizeof pthread_barrierattr_t is 4 bytes\n", sizeof(attr) == 4);

    // Try an invalid initialization (-1 or 0 or a null pthread_barrier_t*)
    retval = pthread_barrier_init(NULL, NULL, 4);
    report("pthread_barrier_init (pthread_barrier_t* == NULL)",
           retval == EINVAL);
    retval = pthread_barrier_init(&barrier, NULL, -1);
    report("pthread_barrier_init (count == -1)", retval == EINVAL);
    retval = pthread_barrier_init(&barrier, NULL, 0);
    report("pthread_barrier_init (count == 0)", retval == EINVAL);
    retval = pthread_barrier_init(&barrier, NULL, INT_MAX);
    report("pthread_barrier_init (count == INT_MAX)", retval == EINVAL);

    // Initalize a barrier with NULL attributes. In general
    // it doesn't really matter what we do with pthread_barrierattr_t
    // PTHREAD_PROCESS_PRIVATE vs PTHREAD_PROCESS_SHARED have the same effect
    // in a unikernel - there's only a single process and all threads can
    // manipulate the barrier so we can just ignore pthread_barrierattr_t
    retval = pthread_barrier_init(&barrier, NULL, numThreads);
    report("pthread_barrier_init", retval == 0);
    if (retval != 0) {
        printf("Early exit, pthread_barrier_init returned %d instead of 0\n",
               retval);
        goto exit;
    }

    for (int t = 0; t < numThreads; t++) {
        threadIds[t] = t;
        retval = pthread_create(&threads[t], NULL, thread_func, &threadIds[t]);
    }
 exit:
    for (int t = 0; t < numThreads; t++) {
        pthread_join(threads[t], NULL);
    }
    report("pthread_barrier_wait (special retvals)",
           specialRetVals.load(std::memory_order_relaxed) == numCrossings);
    pthread_barrier_destroy(&barrier);
    printf("SUMMARY: %u tests / %u failures\n", tests_total, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
