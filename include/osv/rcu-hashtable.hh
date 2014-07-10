/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef RCU_HASHTABLE_HH_
#define RCU_HASHTABLE_HH_

#include <osv/rcu.hh>
#include <vector>

namespace osv {

template <typename T, typename Hash = std::hash<T>>
class rcu_hashtable {
    struct next_ptr {
        rcu_ptr<next_ptr> next = {};
    };
    struct element : next_ptr {
        next_ptr* prev = {};
        T data;

        explicit element(const T& data) : data(data) {}
        element(T&& data) : data(std::move(data)) {}
        template <typename... Arg>
        explicit element(Arg&&... arg) : data(std::forward<Arg>(arg)...) {}
    };
    using bucket_array_type = std::vector<next_ptr>;
    rcu_ptr<bucket_array_type, rcu_deleter<bucket_array_type>> _buckets;
    size_t _size = 0;
    Hash _hash;
public:
    class iterator {
        element* _p = nullptr;
    public:
        iterator(element* p) : _p(p) {}
        iterator(const iterator&) = default;
        iterator() = default;
        T& operator*() const { return _p->data; }
        T* operator->() const { return &_p->data; }
        explicit operator bool() const { return _p; }
        friend rcu_hashtable;
    };
private:
    struct reader_traits {
        template <typename T1, typename Disposer1>
        T1* rcu_read(rcu_ptr<T1, Disposer1>& ptr) const {
            return ptr.read();
        }
    };
    struct owner_traits {
        template <typename T1, typename Disposer1>
        T1* rcu_read(rcu_ptr<T1, Disposer1>& ptr) const {
            return ptr.read_by_owner();
        }
    };
    template <typename Key, typename KeyHash, typename KeyValueCompare, typename FindTraits>
    iterator find(const Key& key, KeyHash key_hash, KeyValueCompare kvc, FindTraits traits);
    void insert(element* e);
    template <typename Func, typename ForTraits>
    void for_each(Func func, ForTraits traits) {
        auto& buckets = *traits.rcu_read(_buckets);
        for (auto& bucket : buckets) {
            auto p = traits.rcu_read(bucket.next);
            while (p) {
                auto q = static_cast<element*>(p);
                func(q->data);
                p = traits.rcu_read(q->next);
            }
        }
    }
    void maybe_grow() {
        auto old_capacity = _buckets.read_by_owner()->size();
        if (old_capacity * 2 <= _size) {
            resize(old_capacity * 2);
        }
    }
    void maybe_shrink() {
        auto old_capacity = _buckets.read_by_owner()->size();
        if (old_capacity / 2 > _size) {
            resize(old_capacity / 2);
        }
    }
    void resize(size_t new_size);
public:
    rcu_hashtable(const rcu_hashtable&) = delete;
    void operator=(const rcu_hashtable&) = delete;
    explicit rcu_hashtable(size_t capacity, Hash hash = Hash())
        : _buckets(new bucket_array_type(capacity)), _hash(hash) {}
    explicit rcu_hashtable(Hash hash = Hash())
        : rcu_hashtable(1, hash) {}
    ~rcu_hashtable();
    template <typename Key, typename KeyHash = std::hash<Key>, typename KeyValueCompare = std::equal_to<T>>
    iterator reader_find(const Key& key, KeyHash key_hash = KeyHash(), KeyValueCompare kvc = KeyValueCompare()) {
        return find(key, key_hash, kvc, reader_traits());
    }
    template <typename Key, typename KeyHash = std::hash<Key>, typename KeyValueCompare = std::equal_to<T>>
    iterator owner_find(const Key& key, KeyHash key_hash = KeyHash(), KeyValueCompare kvc = KeyValueCompare()) {
        return find(key, key_hash, kvc, owner_traits());
    }
    void erase(iterator i);
    void insert(const T& data) {
        insert(new element(data));
    }
    void insert(T&& data) {
        insert(new element(std::move(data)));
    }
    template <typename... Arg>
    void emplace(Arg... arg) {
        insert(new element(std::forward<Arg>(arg)...));
    }
    template <typename Func>
    void owner_for_each(Func func) {
        for_each(func, owner_traits());
    }
    template <typename Func>
    void reader_for_each(Func func) {
        for_each(func, reader_traits());
    }
};

template <typename T, typename Hash>
inline
rcu_hashtable<T, Hash>::~rcu_hashtable()
{
    if (!_buckets.read_by_owner()) {
        return;
    }
    for (auto& b : *_buckets.read_by_owner()) {
        auto n = b.next.read_by_owner();
        b.next.assign(nullptr);
        while (n) {
            auto p = static_cast<element*>(n);
            n = p->next.read_by_owner();
            rcu_dispose(p);
        }
    }
}

template <typename T, typename Hash>
template <typename Key, typename KeyHash, typename KeyValueCompare, typename FindTraits>
inline
auto rcu_hashtable<T, Hash>::find(const Key& key, KeyHash key_hash,
        KeyValueCompare kvc, FindTraits traits) -> iterator {
    auto& buckets = *traits.rcu_read(_buckets);
    auto hash = key_hash(key);
    next_ptr* p = traits.rcu_read(buckets[hash & (buckets.size() - 1)].next);
    while (p) {
        auto q = static_cast<element*>(p);
        if (kvc(key, q->data)) {
            return q;
        }
        p = traits.rcu_read(q->next);
    }
    return nullptr;
}

template <typename T, typename Hash>
inline
void rcu_hashtable<T, Hash>::insert(element* p)
{
    ++_size;
    maybe_grow();
    auto hash = _hash(p->data);
    auto& buckets = *_buckets.read_by_owner();
    next_ptr* first_ptr = &buckets[hash & (buckets.size() - 1)];
    next_ptr* old_first = first_ptr->next.read_by_owner();
    p->next.assign(old_first);
    p->prev = first_ptr;
    if (old_first) {
        static_cast<element*>(old_first)->prev = p;
    }
    first_ptr->next.assign(p);
}

template <typename T, typename Hash>
inline
void rcu_hashtable<T, Hash>::erase(iterator i)
{
    --_size;
    maybe_shrink();
    auto p = i._p;
    next_ptr* next = p->next.read_by_owner();
    next_ptr* prev = p->prev;
    if (next) {
        static_cast<element*>(next)->prev = prev;
    }
    prev->next.assign(next);
    rcu_dispose(p);
}

template <typename T, typename Hash>
void rcu_hashtable<T, Hash>::resize(size_t new_size)
{
    rcu_hashtable n(new_size, _hash);
    owner_for_each([&] (const T& item) {
        n.insert(item);
    });
    auto p = _buckets.read_by_owner();
    _buckets.assign(n._buckets.read_by_owner());
    n._buckets.assign(nullptr);
    rcu_dispose(p);
}

}

#endif /* RCU_HASHTABLE_HH_ */
