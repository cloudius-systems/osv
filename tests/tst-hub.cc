#include "tst-hub.hh"
#include "tst-threads.hh"
#include "tst-malloc.hh"

using namespace unit_tests;

void tests::execute_tests() {
    test_threads threads;
    test_malloc malloc;

    instance().register_test(&threads);
    instance().register_test(&malloc);

    instance().run();
}
