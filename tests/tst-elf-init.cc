/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include <stdio.h>
#include <pthread.h>

void test_symbol_lookup_function(int level) {
    printf("test_symbol_lookup_function at level: %d\n", level);
}

static void* test_symbol_lookup_from_nested_thread_func(void* arg) {
    int *parent_level = (int*)arg;
    test_symbol_lookup_function(*parent_level + 1);
    return nullptr;
}

static void* test_symbol_lookup_from_thread_func(void* arg) {
    int *parent_level = (int*)arg;
    int my_level = *parent_level + 1;
    pthread_t t1;
    pthread_create(&t1, NULL, &test_symbol_lookup_from_nested_thread_func, &my_level);
    pthread_join(t1, NULL);
    test_symbol_lookup_function(my_level);
    return nullptr;
}

void __attribute__((constructor)) test_init() {
    printf("Constructor\n");
    int level = 0;
    pthread_t t1;
    pthread_create(&t1, NULL, &test_symbol_lookup_from_thread_func, &level);
    pthread_join(t1, NULL);
    test_symbol_lookup_function(level);
}

// This tests that dynamic linker properly makes symbols
// visible when calling the INIT function to both the thread which
// executes this functions as well as any threads created by those
// INIT functions
int main() {
    printf("Main\n");
    test_symbol_lookup_function(0);
    return 0;
}