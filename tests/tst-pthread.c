#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>

pthread_mutex_t mutex;
pthread_cond_t cond;
pthread_t thread;
unsigned data;
unsigned acks;

unsigned tests_total, tests_failed;
bool done;

void report(const char* name, bool passed)
{
    static const char* status[] = { "FAIL", "PASS" };
    printf("%s: %s\n", status[passed], name);
    tests_total += 1;
    tests_failed += !passed;
}

void wait_for_ack(unsigned n)
{
    pthread_mutex_lock(&mutex);
    while (acks != n) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
}

void post_ack()
{
    ++acks;
    pthread_cond_signal(&cond);
}

void* secondary(void *ignore)
{
    struct timespec ts;
    struct timeval tv;
    int r;

    printf("starting secondary\n");
    pthread_mutex_lock(&mutex);
    r = 0;
    while (data != 1 && r == 0) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    report("pthread_cond_wait", r == 0);

    post_ack(1);

    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + 1000000;
    ts.tv_nsec = 0;
    pthread_mutex_lock(&mutex);
    r = 0;
    while (data != 2 && r == 0) {
        r = pthread_cond_timedwait(&cond, &mutex, &ts);
    }
    pthread_mutex_unlock(&mutex);
    report("pthread_cond_timedwait (long)", r == 0);

    post_ack(2);

    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000 + 1000000;
    pthread_mutex_lock(&mutex);
    r = 0;
    while (data != 666 && r == 0) {
        r = pthread_cond_timedwait(&cond, &mutex, &ts);
    }
    pthread_mutex_unlock(&mutex);
    report("pthread_cond_timedwait (short)", r == ETIMEDOUT);

    post_ack(3);

    done = true;
    pthread_cond_signal(&cond);
    return NULL;
}

int main(void)
{
    printf("starting pthread test\n");
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    pthread_create(&thread, NULL, secondary, NULL);

    pthread_mutex_lock(&mutex);
    data = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);

    wait_for_ack(1);

    pthread_mutex_lock(&mutex);
    data = 2;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);

    wait_for_ack(2);

    wait_for_ack(3);

    // FIXME: join instead
    pthread_mutex_lock(&mutex);
    while (!done) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    void* ret;
    pthread_join(thread, &ret);

    printf("SUMMARY: %u tests / %u failures", tests_total, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
