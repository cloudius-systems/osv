/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#ifndef _ASYNC_HH
#define _ASYNC_HH

#include <atomic>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/waitqueue.hh>
#include <osv/condvar.h>
#include <osv/clock.hh>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>

namespace async {

namespace bi = boost::intrusive;

using clock = osv::clock::uptime;
using callback_t = std::function<void()>;

class async_worker;
class timer_task;

struct percpu_timer_task {
    enum class state {
        ACTIVE, FIRING, RELEASED
    };

    percpu_timer_task(async_worker& worker)
        : _worker(worker)
        , next(nullptr)
        , _state(state::ACTIVE)
        , master(nullptr)
        , queued(false)
    {
    }

    percpu_timer_task(const percpu_timer_task&) = delete;

    async_worker& _worker;

    bi::set_member_hook<> hook;

    bi::list_member_hook<> registrations_hook;

    // async_worker::released_timer_tasks link
    percpu_timer_task* next;

    // Used to coordinate timer_task shutdown with callback firing
    std::atomic<state> _state;

    // All fields below must not change while queued
    timer_task* master;
    clock::time_point fire_at;
    bool queued;

    friend bool operator<(const percpu_timer_task& a, const percpu_timer_task& b) {
        return a.fire_at < b.fire_at;
    }
};


/**
 *  Represents a task which can be scheduled to execute in future.
 *  Task which has not yet fired can be canceled. Task can be rescheduled
 *  to fire at different time.
 *
 *  Example:
 *
 *    timer_task task([] {
 *       // Hi, callback here
 *    });
 *
 *    task.reschedule(10_ms);
 *
 *  There is a single total order of all operations performed on
 *  particular object, observed by all threads.
 *
 *  Callbacks are executed only after given time point is reached.
 *  They are executed in a regular thread's context. The order in
 *  which callbacks fire is unspecified. Callbacks may fire
 *  concurrently, that applies even to callbacks of the same
 *  timer_task object.
 *
 *  This object must not be destroyed from the callback, otherwise
 *  a deadlock may occur.
 */
class timer_task {
public:
    timer_task(callback_t&& callback);
    timer_task(const timer_task&) = delete;
    ~timer_task();

    /**
     * Schedules the callback to run after given time_point has passed.
     * Previously scheduled callback is cancelled.
     *
     * Can be called from a callback.
     *
     * Return value has the same meaning as that of cancel().
     */
    bool reschedule(clock::time_point time_point);

    /**
     * Equivalent of reschedule(clock::now() + delay)
     */
    bool reschedule(clock::duration delay);

    /**
     * Cancels this task.
     *
     * Returns true if callback was pending and was cancelled.
     * If false is returned then either this task was not scheduled before or
     * the callback has already fired (the callback may be at any execution point).
     *
     * Can be called from a callback.
     */
    bool cancel();

    /**
     * Returns true if and only if the task is scheduled and has not yet fired.
     * Inherently racy, use with caution.
     */
    bool is_pending();
private:
    friend class async_worker;
    void fire(percpu_timer_task&);
    void free_registration(percpu_timer_task& task);

    bi::list<percpu_timer_task,
        bi::member_hook<percpu_timer_task,
            bi::list_member_hook<>,
            &percpu_timer_task::registrations_hook>> _registrations;

    percpu_timer_task* _active_task;

    mutex _mutex;
    waitqueue _registrations_drained;

    callback_t _callback;
    bool _terminating;
};

/**
 * Specialized version of timer_task which serializes all
 * timer operations and callbacks using supplied lock.
 *
 * All methods should be invoked with the serializing lock held.
 *
 * The callback needs to consult try_fire() after it acquired the lock
 * but before it does its task, to check if it hasn't been cancelled
 * or rescheduled.
 */
class serial_timer_task {
public:
    using callback_t = std::function<void(serial_timer_task&)>;

    serial_timer_task(mutex& lock, callback_t&& callback);
    ~serial_timer_task();

    /**
     * Schedules callback to run in given amount of time from now.
     * If any callback is currently pending it is cancelled.
     *
     * Callbacks which have already fired but have not yet called
     * try_fire() will be rejected when they call try_fire(). If there
     * are multiple callbacks waiting to take the lock, only one of them
     * will be allowed to pass by try_lock().
     */
    void reschedule(clock::duration delay);

    /**
     * Cancels pending callback, if any. Does not wait for callbacks which
     * are currently running.
     */
    void cancel();

    /**
     * Cancels pending callback if any and waits for callbacks which fired but
     * have not yet finished. A callback is considered to finish firing after it
     * called try_fire() and released the lock.
     *
     * This method should be used to safely cancel this timer before it is
     * deleted.
     *
     * This method may drop the lock temporarily.
     */
    void cancel_sync();

    /**
     * Timer is active if it has been scheduled but has not yet fired. The moment
     * of firing happens when try_fire() is called.
     */
    bool is_active();

    /**
     * Should be called from the callback to determine if the callback
     * is eligible to fire but without marking this callback as have fired (yet).
     * Returns true if the callback can proceed.
     */
    bool can_fire();

    /**
     * Should be called by the callback after taking the lock but before taking any
     * action to check if it is eligible to fire. It can proceed only if this returns true.
     */
    bool try_fire();

private:
    bool _active;
    int _n_scheduled;
    mutex& _lock;
    timer_task _task;
    waitqueue _all_done;
};

/**
 * Schedules given callback for execution in worker's thread. It will be executed as
 * soon as possible although there are no guarantees as for when that will happen.
 */
void run_later(callback_t&& callback);

}

#endif
