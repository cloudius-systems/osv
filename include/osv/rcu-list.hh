/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef RCU_LIST_HH_
#define RCU_LIST_HH_

#include <osv/rcu.hh>
#include <memory>

namespace osv {

// rcu-capable singly linked list.
//
// Two interfaces are provided: by_owner() returns a mutable
// list interface that can be used with exclusive locking,
// while for_read() returns a read-only interface that can
// be used concurrently while holding rcu_read_lock.
template <typename T>
class rcu_list {
private:
    struct element;
    using pointer = rcu_ptr<element>;
    struct element {
        element(element* next, const T& data) noexcept(noexcept(T(data)))
                : next(next.read_by_owner()), data(data) {}
        template <typename... Args>
        element(element* next, Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...)))
            : next(next), data(std::forward<Args>(args)...) {}
        pointer next;
        T data;
    };
private:
    pointer _first = {};
public:
    class mutable_list;
    class read_only_list;
public:
    rcu_list() noexcept {}
    ~rcu_list() noexcept(noexcept(delete _first.read_by_owner()));
    rcu_list(const rcu_list& other) = delete;
    void operator=(const rcu_list& other) = delete;
    mutable_list by_owner() noexcept;
    read_only_list for_read() noexcept;
    friend mutable_list;
    friend read_only_list;
};

template <typename T>
rcu_list<T>::~rcu_list() noexcept(noexcept(delete _first.read_by_owner()))
{
    element* e = _first.read_by_owner();
    while (e) {
        auto o = e;
        e = e->next.read_by_owner();
        delete o;
    }
}

template <typename T>
class rcu_list<T>::read_only_list {
    friend rcu_list<T>;
public:
    class iterator;
    friend iterator;
private:
    using pointer = rcu_list<T>::pointer;
    using element = rcu_list<T>::element;
    pointer& _first;
public:
    class iterator {
        friend read_only_list;
    private:
        element* _p;
    private:
        explicit iterator(element* p) noexcept : _p(p) {}
    public:
        T& operator*() const noexcept { return _p->data; }
        T* operator->() const noexcept { return &_p->data; }
        iterator& operator++() noexcept { _p = _p->next.read(); return *this; }
        iterator& operator++(int) noexcept { auto old = *this; ++*this; return old; }
        bool operator==(const iterator& other) const noexcept { return _p == other._p; }
        bool operator!=(const iterator& other) const noexcept { return !operator==(other); }
    };
private:
    explicit read_only_list(rcu_list<T>& l) noexcept : _first(l._first) {}
public:
    iterator begin() const noexcept { return iterator(_first.read()); }
    iterator end() const noexcept { return iterator(nullptr); }
};

template <typename T>
class rcu_list<T>::mutable_list {
public:
    class iterator;
    friend iterator;
private:
    using pointer = rcu_list<T>::pointer;
    using element = rcu_list<T>::element;
    pointer& _first;
public:
    class iterator {
        friend mutable_list;
    private:
        pointer* _pp;
        element* _p;
    private:
        explicit iterator() noexcept : _pp(nullptr), _p(nullptr) {}
        explicit iterator(pointer& pp) noexcept : _pp(&pp), _p(pp.read_by_owner()) {}
    public:
        T& operator*() const noexcept { return *_p; }
        T* operator->() const noexcept { return _p; }
        iterator& operator++() noexcept { _pp = &_p->next; _p = _pp->read_by_owner(); return *this; }
        iterator& operator++(int) noexcept { auto old = *this; ++*this; return old; }
        bool operator==(const iterator& other) const noexcept { return _p == other._p; }
        bool operator!=(const iterator& other) const noexcept { return !operator==(other); }
    };
private:
    mutable_list(rcu_list<T>& list) noexcept : _first(list._first) {}
public:
    iterator begin() noexcept { return iterator(_first); }
    iterator end() noexcept { return iterator(); }
    void push_front(const T& data) noexcept(noexcept(T(data)));
    template <typename... Args>
    void emplace_front(Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...)));
    void erase(iterator i) noexcept;
    static_assert(noexcept(static_cast<T*>(nullptr)->~T()), "T::~T() may not throw");
    friend rcu_list<T>;
};

template <typename T>
inline
auto rcu_list<T>::for_read() noexcept -> read_only_list
{
    return read_only_list(*this);
}

template <typename T>
inline
auto rcu_list<T>::by_owner() noexcept -> mutable_list
{
    return mutable_list(*this);
}

template <typename T>
inline
void rcu_list<T>::mutable_list::push_front(const T& data) noexcept(noexcept(T(data)))
{
    _first.assign(new element(_first.read_by_owner(), data));
}

template <typename T>
template <typename... Args>
inline
void rcu_list<T>::mutable_list::emplace_front(Args&&... args)
    noexcept(noexcept(T(std::forward<Args>(args)...)))
{
    _first.assign(new element(_first.read_by_owner(), std::forward<Args>(args)...));
}

template <typename T>
inline
void rcu_list<T>::mutable_list::erase(rcu_list<T>::mutable_list::iterator i) noexcept
{
    auto cur = i._p;
    auto n = i._p->next.read_by_owner();
    i._pp->assign(n);
    rcu_dispose(cur);
}

}

#endif /* RCU_LIST_HH_ */
