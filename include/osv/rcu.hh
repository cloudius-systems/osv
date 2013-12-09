/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef RCU_HH_
#define RCU_HH_

#include <atomic>
#include <memory>
#include <functional>
#include <barrier.hh>

// Read-copy-update implementation
//
// This provides a reader/writer synchronization mechanism with
// a very lightweight read path, and a very expensive write path.
//
// The basic premise behind RCU is that the read path only indicates
// the critical section in which the protected data is used (in our
// implementation, by disabling preemption).  The write path never
// modifies the data directly; instead it creates a new copy and
// replaces the old copy atomically.  The old copy is then disposed
// of after all readers have finished using it.
//
//
// How to use:
//
//    Declaring:
//    
//       mutex mtx;
//       rcu_ptr<my_object> my_ptr;
//    
//    Read-side:
//    
//       WITH_LOCK(rcu_read_lock) {
//          const my_object* p = my_ptr.read();
//          // do things with *p
//          // but don't block!
//       }
//    
//    Write-side:
//    
//      WITH_LOCK(mtx) {
//        my_object* old = my_ptr.read_by_owner();
//        my_object* p = new my_object;
//        // ...
//        my_ptr.assign(p);
//        rcu_dispose(old);  // or rcu_defer(some_func, old);
//      }
//

// forward-declare some stuff to avoid #include hell
namespace sched {

void preempt_disable();
void preempt_enable();

}

namespace osv {

class rcu_lock_type {
public:
    static void lock();
    static void unlock();
};

class preempt_lock_in_rcu_type {
public:
    // our rcu implementation disables preemption, so nothing further needs to be done
    static void lock() {}
    static void unlock() {}
};

extern rcu_lock_type rcu_read_lock;
extern preempt_lock_in_rcu_type preempt_lock_in_rcu;

template <typename T>
class rcu_ptr {
public:
    // Access contents for reading.  Note: must be only called once
    // for an object within a lock()/unlock() pair.
    T* read() const;
    // Update contents.  Note: must not be called concurrently with
    // other assign() calls to the same objects.
    void assign(T* p);
    // Access contents, must be called with exclusive access wrt.
    // mutator (i.e. in same context as assign().
    T* read_by_owner();
    // Check if the pointer is non-null, can be done outside
    // rcu_read_lock
    operator bool() const;
private:
    std::atomic<T*> _ptr;
};

// Calls 'delete p' when it is safe to do so
template <typename T>
void rcu_dispose(T* p);

// Calls 'delete[] p' when it is safe to do so
template <typename T>
void dispose_array(T* p);

// Calls 'func(p)' when it is safe to do so
template <typename T, typename functor>
static void rcu_defer(functor func, T* p);

// Calls 'func()' when it is safe to do so
void rcu_defer(std::function<void ()>&& func);

void rcu_init();

///////////////

inline void rcu_lock_type::lock()
{
    sched::preempt_disable();
}

inline void rcu_lock_type::unlock()
{
    // Prevent reads from being hoisted after unlock(), since the memory
    // can be destroyed.
    // This is implied by preempt_enable(), since it calls schedule(), but
    // let's make it explicit anyway.
    barrier();
    sched::preempt_enable();
}

template <typename T>
inline
T* rcu_ptr<T>::read() const
{
    return _ptr.load(std::memory_order_consume);
}

template <typename T>
inline
void rcu_ptr<T>::assign(T* p)
{
    _ptr.store(p, std::memory_order_release);
}

template <typename T>
inline
rcu_ptr<T>::operator bool() const
{
    return _ptr.load(std::memory_order_relaxed);
}

template <typename T>
inline
void rcu_dispose(T* p)
{
    rcu_defer(std::default_delete<T>(), p);
}

template <typename T>
inline
T* rcu_ptr<T>::read_by_owner()
{
    return _ptr.load(std::memory_order_relaxed);
}

template <typename T>
inline
void rcu_dispose_array(T* p)
{
    rcu_defer(std::default_delete<T[]>(), p);
}

template <typename T, typename functor>
inline
void rcu_defer(functor func, T* p)
{
    rcu_defer([=] { func(p); });
}

void rcu_synchronize();

}

#endif /* RCU_HH_ */
