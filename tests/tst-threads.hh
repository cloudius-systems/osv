struct test_threads_data {
    thread* main;
    thread* t1;
    bool t1ok;
    thread* t2;
    bool t2ok;
};

void test_thread_1(test_threads_data& tt)
{
    while (test_ctr < 1000) {
        thread::wait_until([&] { return (test_ctr % 2) == 0; });
        ++test_ctr;
        if (tt.t2ok) {
            tt.t2->wake();
        }
    }
    tt.t1ok = false;
    tt.main->wake();
}

void test_thread_2(test_threads_data& tt)
{
    while (test_ctr < 1000) {
        thread::wait_until([&] { return (test_ctr % 2) == 1; });
        ++test_ctr;
        if (tt.t1ok) {
            tt.t1->wake();
        }
    }
    tt.t2ok = false;
    tt.main->wake();
}

void test_threads()
{
    test_threads_data tt;
    tt.main = thread::current();
    tt.t1ok = tt.t2ok = true;
    tt.t1 = new thread([&] { test_thread_1(tt); });
    tt.t2 = new thread([&] { test_thread_2(tt); });
    thread::wait_until([&] { return test_ctr >= 1000; });
    tt.t1->join();
    tt.t2->join();
    delete tt.t1;
    delete tt.t2;
    debug("threading test succeeded");
}
