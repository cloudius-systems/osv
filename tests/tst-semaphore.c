/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <assert.h>

#define THREAD_NUMBER 10

static sem_t sem_sync, sem_done;
static int counter = 0;

static void* threadfunc(void* arg) {
    int *thread_number = (int*)arg;
    for (int i = 0; i < 10; i++) {
        assert(sem_wait(&sem_sync) == 0);
        counter++;
        assert(sem_post(&sem_sync) == 0);
        printf("Thread %d: Incremented %dth\n", *thread_number, i + 1);
        usleep(1000 * (counter % 5)); // Pseudo-randomly sleep between 0-4ms
    }
    assert(sem_post(&sem_done) == 0);
    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_NUMBER];
    int threads_numbers[THREAD_NUMBER];

    assert(sem_init(&sem_sync, 0, 1) == 0);
    assert(sem_init(&sem_done, 0, 0) == 0);

    for (int t = 0; t < THREAD_NUMBER; t++) {
        threads_numbers[t] = t + 1;
        pthread_create(threads + t, NULL, &threadfunc, threads_numbers + t);
    }

    for (int t = 0; t < THREAD_NUMBER; t++) {
        while(sem_trywait(&sem_done));
    }

    assert(counter == 10 * THREAD_NUMBER);

    for (int t = 0; t < THREAD_NUMBER; t++) {
        pthread_join(threads[t], NULL);
    }

    assert(sem_destroy(&sem_sync) == 0);
    assert(sem_destroy(&sem_done) == 0);

    return 0;
}
