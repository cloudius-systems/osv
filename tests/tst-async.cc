/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-async

#include <osv/async.hh>
#include <osv/clock.hh>
#include <osv/trace.hh>
#include <osv/preempt-lock.hh>
#include <boost/test/unit_test.hpp>
#include <future>

using namespace osv::clock::literals;
using namespace async;

template<typename T, typename Duration>
static void assert_resolves(std::promise<T>& promise, Duration duration)
{
    BOOST_REQUIRE(promise.get_future().wait_for(duration) == std::future_status::ready);
}

template<typename T, typename Duration>
static void assert_not_resolved(std::promise<T>& promise, Duration duration)
{
    BOOST_REQUIRE(promise.get_future().wait_for(duration) == std::future_status::timeout);
}

BOOST_AUTO_TEST_CASE(test_one_shot_task_fires_soon)
{
    auto done = std::make_shared<std::promise<bool>>();

    run_later([=] {
        done->set_value(true);
    });

    assert_resolves(*done, 10_ms);
}

BOOST_AUTO_TEST_CASE(test_async_task_fires)
{
    std::promise<bool> done;

    timer_task task([&] {
        done.set_value(true);
    });

    task.reschedule(100_ms);
    assert_resolves(done, 200_ms);
}

BOOST_AUTO_TEST_CASE(test_async_task_can_be_cancelled)
{
    std::promise<bool> done;

    timer_task task([&] {
        done.set_value(true);
    });

    task.reschedule(100_ms);
    BOOST_REQUIRE(task.cancel());

    assert_not_resolved(done, 200_ms);
}

BOOST_AUTO_TEST_CASE(test_async_task_can_be_reprogrammed_while_armed)
{
    std::promise<bool> done;

    timer_task task([&] {
        done.set_value(true);
    });

    task.reschedule(100_ms);
    task.reschedule(10_ms);

    assert_resolves(done, 20_ms);
}

BOOST_AUTO_TEST_CASE(test_async_task_can_be_reprogrammed_after_cancelled)
{
    std::promise<bool> done;

    timer_task task([&] {
        done.set_value(true);
    });

    task.reschedule(100_ms);
    BOOST_REQUIRE(task.cancel());
    task.reschedule(10_ms);

    assert_resolves(done, 20_ms);
}

BOOST_AUTO_TEST_CASE(test_desctructor_does_not_block_when_called_after_task_is_done)
{
    std::promise<bool> done;

    timer_task task([&] {
        done.set_value(true);
    });

    task.reschedule(1_ms);
    assert_resolves(done, 10_ms);
}

BOOST_AUTO_TEST_CASE(test_destructor_waits_for_callback_to_finish)
{
    std::atomic<int> counter {0};
    std::promise<bool> proceed;

    {
        timer_task task([&] {
            proceed.set_value(true);

            // if destructor didn't wait for us then counter would be still 0 when
            // it is checked later for value of 1
            std::this_thread::sleep_for(10_ms);

            counter++;
        });

        task.reschedule(20_ms);

        assert_resolves(proceed, 30_ms);
    }

    BOOST_REQUIRE(counter == 1);
}

BOOST_AUTO_TEST_CASE(test_async_task_can_be_reprogrammed_after_done)
{
    std::atomic<int> counter {0};
    std::promise<bool> proceed1;
    std::promise<bool> proceed2;

    {
        timer_task task([&] {
            if (counter++ == 0) {
                proceed1.set_value(true);
            } else {
                proceed2.set_value(true);
            }
        });

        task.reschedule(1_ms);
        assert_resolves(proceed1, 20_ms);
        BOOST_REQUIRE(counter == 1);

        task.reschedule(1_ms);
        assert_resolves(proceed2, 20_ms);
    }

    BOOST_REQUIRE(counter == 2);
}

BOOST_AUTO_TEST_CASE(test_async_task_can_be_reprogrammed_from_the_callback)
{
    std::atomic<int> counter {0};
    std::promise<bool> done;

    timer_task* task;

    task = new timer_task([&] {
        if (counter++ == 0) {
            task->reschedule(1_ms);
        } else {
            done.set_value(true);
        }
    });

    task->reschedule(1_ms);

    assert_resolves(done, 100_ms);
    BOOST_REQUIRE(counter == 2);

    delete task;
}

BOOST_AUTO_TEST_CASE(test_destructor_waits_for_callbacks_which_have_rescheduled_themselves)
{
    std::atomic<int> counter {0};
    std::promise<bool> checkpoint;

    timer_task* task;

    task = new timer_task([&] {
        if (counter++ == 0) {
            task->reschedule(10_s); // should never fire
            checkpoint.set_value(true);
            std::this_thread::sleep_for(100_ms);
            counter++;
        } else {
            abort();
        }
    });

    task->reschedule(1_ms);

    assert_resolves(checkpoint, 100_ms);
    delete task;

    BOOST_REQUIRE(counter == 2);
}

BOOST_AUTO_TEST_CASE(test_a_task_which_is_set_far_in_future_does_not_block_new_task)
{
    std::promise<bool> done;

    timer_task far_task([&] {});
    far_task.reschedule(5_s);

    timer_task task([&] {
        done.set_value(true);
    });
    task.reschedule(1_ms);

    assert_resolves(done, 10_ms);
}

BOOST_AUTO_TEST_CASE(test_task_which_is_scheduled_second_but_with_sooner_expiration_time_fires_first)
{
    mutex lock;
    std::vector<int> values;
    std::promise<bool> done;

    timer_task task_1([&] {
        WITH_LOCK(lock) {
            values.push_back(1);
        }
        done.set_value(true);
    });

    timer_task task_2([&] {
        WITH_LOCK(lock) {
            values.push_back(2);
        }
    });

    WITH_LOCK(preempt_lock) { // So that both end up in the same async_worker
        task_1.reschedule(10_ms);
        task_2.reschedule(1_ms);
    }

    assert_resolves(done, 1000_ms);

    BOOST_REQUIRE(values.size() == 2);
    BOOST_REQUIRE(values[0] == 2);
    BOOST_REQUIRE(values[1] == 1);
}

BOOST_AUTO_TEST_CASE(test_timers_with_same_expiration_time_fire_separately)
{
    std::promise<bool> done;
    std::atomic<int> counter {0};
    const int n_tasks = 10;
    timer_task* tasks[n_tasks];

    for (auto& task_ptr : tasks) {
        task_ptr = new timer_task([&] {
            if (++counter == n_tasks) {
                done.set_value(true);
            }
        });
    }

    auto deadline = async::clock::now() + 1_ms;

    for (auto& task_ptr : tasks) {
        task_ptr->reschedule(deadline);
    }

    assert_resolves(done, 15_ms);

    for (auto& task_ptr : tasks) {
        delete task_ptr;
    }
}

BOOST_AUTO_TEST_CASE(test_is_pending)
{
    timer_task task([&] {});
    task.reschedule(1_s);

    BOOST_REQUIRE(task.is_pending());
    BOOST_REQUIRE(task.cancel());
    BOOST_REQUIRE(!task.is_pending());
}

BOOST_AUTO_TEST_CASE(test_serial_timer__cancel_sync_waits_for_callback)
{
    std::atomic<int> counter {0};
    std::promise<bool> proceed;

    mutex lock;

    serial_timer_task task(lock, [&] (serial_timer_task& timer) {
            proceed.set_value(true);

            std::this_thread::sleep_for(10_ms);
            WITH_LOCK(lock) {
                counter++;
                if (timer.try_fire()) {
                    std::cerr << "Should have been cancelled";
                    abort();
                }
            }
    });

    WITH_LOCK(lock) {
        task.reschedule(1_ms);
    }

    assert_resolves(proceed, 100_ms);

    WITH_LOCK(lock) {
        task.cancel_sync();
        BOOST_REQUIRE(counter == 1);
    }
}

BOOST_AUTO_TEST_CASE(test_serial_timer__cancel_sync_waits_for_many_callbacks)
{
    std::atomic<int> counter {0};
    std::promise<bool> proceed;

    mutex lock;

    serial_timer_task task(lock, [&] (serial_timer_task& timer) {
        if (counter++ == 0) {
            WITH_LOCK(lock) {
                timer.reschedule(1_ms);
            }
        } else {
            proceed.set_value(true);
        }

        std::this_thread::sleep_for(10_ms);

        WITH_LOCK(lock) {
            counter++;
            if (timer.try_fire()) {
                std::cerr << "Should have been cancelled";
                abort();
            }
        }
    });

    WITH_LOCK(lock) {
        task.reschedule(1_ms);
    }

    assert_resolves(proceed, 100_ms);

    WITH_LOCK(lock) {
        task.cancel_sync();
        BOOST_REQUIRE(counter == 4);
    }
}

BOOST_AUTO_TEST_CASE(test_serial_timer__cancel_sync_does_not_wait_if_callback_has_not_yet_fired)
{
    mutex lock;

    serial_timer_task task(lock, [&] (serial_timer_task& timer) {
        abort();
    });

    WITH_LOCK(lock) {
        task.reschedule(2_s);
    }

    WITH_LOCK(lock) {
        task.cancel_sync();
    }
}

BOOST_AUTO_TEST_CASE(test_serial_timer__callback_fires_if_not_cancelled)
{
    std::promise<bool> proceed;
    int counter = 0;
    mutex lock;

    serial_timer_task task(lock, [&] (serial_timer_task& timer) {
        proceed.set_value(true);

        WITH_LOCK(lock) {
            if (timer.try_fire()) {
                counter++;
            }
        }
    });

    WITH_LOCK(lock) {
        task.reschedule(1_ms);
    }

    assert_resolves(proceed, 100_ms);

    WITH_LOCK(lock) {
        task.cancel_sync();
    }

    BOOST_REQUIRE(counter == 1);
}
