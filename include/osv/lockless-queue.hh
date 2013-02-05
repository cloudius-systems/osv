#ifndef LOCKLESS_QUEUE_HH_
#define LOCKLESS_QUEUE_HH_

#include <atomic>

template <class T> class lockless_queue_link;
template <class T, lockless_queue_link<T> T::*link> class lockless_queue;

// single producer, single consumer, lockless

template <class T, lockless_queue_link<T> T::*link>
class lockless_queue {
public:
    lockless_queue();
    bool empty() const;
    void copy_and_clear(lockless_queue& to);
    void push_front(T& elem);
    T& front();
    void pop_front_nonatomic();
private:
    std::atomic<T*> _head;
};

template <class T>
class lockless_queue_link
{
public:
    T* _next;
};

template <class T, lockless_queue_link<T> T::*link>
lockless_queue<T, link>::lockless_queue()
    : _head(nullptr)
{

}

template <class T, lockless_queue_link<T> T::*link>
void lockless_queue<T, link>::copy_and_clear(lockless_queue& to)
{
    to._head = _head.exchange(nullptr);
}

template <class T, lockless_queue_link<T> T::*link>
void lockless_queue<T, link>::push_front(T& elem)
{
    // no ABA, since we are sole producer
    T* old;
    do {
        old = _head.load();
        (elem.*link)._next = old;
    } while (!_head.compare_exchange_weak(old, &elem));
}

template <class T, lockless_queue_link<T> T::*link>
bool lockless_queue<T, link>::empty() const
{
    return !_head.load();
}

template <class T, lockless_queue_link<T> T::*link>
T& lockless_queue<T, link>::front()
{
    return *_head.load();
}

template <class T, lockless_queue_link<T> T::*link>
void lockless_queue<T, link>::pop_front_nonatomic()
{
    _head = (_head.load()->*link)._next;
}

#endif /* LOCKLESS_QUEUE_HH_ */
