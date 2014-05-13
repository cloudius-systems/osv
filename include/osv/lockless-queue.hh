/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef LOCKLESS_QUEUE_HH_
#define LOCKLESS_QUEUE_HH_

#include <atomic>

#include <arch.hh>

template <class T> struct lockless_queue_helper;
template <class T> class lockless_queue_link;
template <class T, lockless_queue_link<T> T::*link> class lockless_queue;

// single producer, single consumer, lockless

template <class T, lockless_queue_link<T> T::*link>
class lockless_queue {
public:
    lockless_queue();
    ~lockless_queue();
    bool empty() const;
    void push_back(T& elem);
    T& front();
    void pop_front();
private:
    lockless_queue_helper<T>* _head CACHELINE_ALIGNED;
    lockless_queue_helper<T>* _tail CACHELINE_ALIGNED;
};

template <class T>
struct lockless_queue_helper {
    std::atomic<T*> _next;
};

template <class T>
class lockless_queue_link
{
public:
    lockless_queue_link();
    ~lockless_queue_link();

    lockless_queue_helper<T>* _helper;
    lockless_queue_helper<T>* _next;
};

template <class T>
lockless_queue_link<T>::lockless_queue_link()
    : _helper(new lockless_queue_helper<T>)
{
}

template <class T>
lockless_queue_link<T>::~lockless_queue_link()
{
    delete _helper;
}

template <class T, lockless_queue_link<T> T::*link>
lockless_queue<T, link>::lockless_queue()
    : _head(new lockless_queue_helper<T>)
    , _tail(_head)
{
    _head->_next.store(nullptr, std::memory_order_relaxed);
}

template <class T, lockless_queue_link<T> T::*link>
lockless_queue<T, link>::~lockless_queue()
{
    delete _tail;
}

template <class T, lockless_queue_link<T> T::*link>
bool lockless_queue<T, link>::empty() const
{
    return !_head->_next.load(std::memory_order_relaxed);
}

template <class T, lockless_queue_link<T> T::*link>
void lockless_queue<T, link>::push_back(T& elem)
{
    lockless_queue_helper<T>* helper = (elem.*link)._helper;
    (elem.*link)._next = helper;
    (elem.*link)._helper = _tail;
    helper->_next.store(nullptr, std::memory_order_relaxed);
    _tail->_next.store(&elem, std::memory_order_release);
    _tail = helper;
}

template <class T, lockless_queue_link<T> T::*link>
T& lockless_queue<T, link>::front()
{
    return *_head->_next.load(std::memory_order_consume);
}

template <class T, lockless_queue_link<T> T::*link>
void lockless_queue<T, link>::pop_front()
{
    auto elem = _head->_next.load(std::memory_order_consume);
    _head = (elem->*link)._next;
}

#endif /* LOCKLESS_QUEUE_HH_ */
