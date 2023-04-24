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
#include <fcntl.h>

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

    ///Named sempahore test

    //Create and open two handles to a named semaphore
    sem_t *named_sem1 = sem_open("name", O_CREAT, 0777, 1);
    assert(named_sem1 != SEM_FAILED);
    sem_t *named_sem2 = sem_open("name", O_EXCL, 0, 0);
    assert(named_sem1 == named_sem2);

    //Can't create a new named semaphore without O_CREAT
    assert(sem_open("other", 0, 0777, 1) == SEM_FAILED);
    assert(sem_open("other", O_EXCL | O_SYNC, 0777, 1) == SEM_FAILED); 

    //Any other flags should have no effect if the named semaphore does not exist
    sem_t *named_sem3 = sem_open("other", O_EXCL | O_CREAT | O_SYNC, 0777, 1);
    assert(named_sem3 != SEM_FAILED);
    assert(sem_unlink("other") == 0);
    assert(sem_close(named_sem3) == 0);

    //Close both handles to the semaphore without removing the name
    assert(sem_close(named_sem1) == 0);
    assert(sem_close(named_sem2) == 0);

    //Open two more handles to the named sempahore
    named_sem1 = sem_open("name", 0);
    assert(named_sem1 != SEM_FAILED);
    named_sem2 = sem_open("name", 0);
    assert(named_sem1 == named_sem2);

    //Can't open existing semaphore with O_CREAT and O_EXCL set
    assert(sem_open("name", O_CREAT | O_EXCL, 0777, 1) == SEM_FAILED);
    assert(sem_open("name", O_CREAT | O_EXCL | O_SYNC, 0777, 1) == SEM_FAILED);

    //Unlink the named semaphore. Can't open more handles.
    assert(sem_unlink("name") == 0);
    assert(sem_unlink("name") == -1);
    assert(sem_open("name", 0) == SEM_FAILED);

    //Close handles
    assert(sem_close(named_sem1) == 0);
    assert(sem_close(named_sem2) == 0);

    return 0;
}
