#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <atomic>

static std::atomic<bool> mmx_finished = { false };

static void *mmx(void *arg) {
    printf("MMX START: %f\n", 10.5);

    // Execute some non-sensical code using MMX instructions
    // in long-enough loop to be preempted by OSv thread scheduler
    for (unsigned long i = 0; i < 500000000l; i++) {
        asm volatile ("movq $0x0fffffff, %rdx \n\t");

        asm volatile ("movq %rdx, %mm0 \n\t");
        asm volatile ("movq %rdx, %mm1 \n\t");
        asm volatile ("paddsb %mm0, %mm1 \n\t");
    }
    asm volatile ("emms");

    printf("MMX END: %f\n", 11.5);
    return NULL;
}

static void *fpu(void *arg) {
    printf("FPU START: %f\n", 10.5);

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000;

    char buffer[10];
    while (!mmx_finished.load()) {
        sprintf(buffer, "%f", 0.5);
        // Verify that sprintf, that uses FPU, prints
        // correct representation of 0.5
        assert(!strcmp(buffer, "0.500000"));
        nanosleep(&ts, NULL);
    }

    printf("FPU END: %f\n", 11.5);
    return NULL;
}

// This test verifies that two threads - one using MMX instructions
// and another one using FPU and yielding (see sleep()) - can execute
// correctly without affecting each other. To that end OSv
// needs to make sure that CPU is properly reset after MMX code
// gets preempted so that code on new thread can safely execute
// code using traditional FPU instructions. For details please
// see issue #1019.
int main() {
    printf("Hello from C code, %f\n", NAN);

    pthread_t mmx_thread, fpu_thread;

    pthread_create(&mmx_thread, NULL, mmx, NULL);
    pthread_create(&fpu_thread, NULL, fpu, NULL);

    pthread_join(mmx_thread, NULL);
    mmx_finished.store(true);
    pthread_join(fpu_thread, NULL);

    return 0;
}
