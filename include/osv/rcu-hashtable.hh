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

/// rcu-protected hash table.
///
/// This hash table template provides insert, delete, and
/// find operations that require external synchronization
/// (e.g. a \ref ::mutex) and also a lookup operation that is protected
/// by RCU (e.g. \ref osv::rcu_read_lock).
///
/// The hash table grows and shrinks automatically to maintain
/// O(1) time amortized lookup, insert, and delete operations.
///
/// The two find operations (reader_find() and owner_find())
/// Can be used with a key that is the same type as the data
/// element #T (like std::unordered_set), or they can be used
/// with a different key type (typically a field within \ref T), in
/// which case a hash functor and a comparator must also be supplied.
///
/// Data elements must support copy construction.
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
    std::atomic<size_t> _size = { 0 };
    Hash _hash;
public:
    /// A pointer to an element.
    ///
    /// Iterators obtained by owner_find() are invalidated by insert
    /// and delete operations.
    ///
    /// Iterators obtained by reader_find() are only valid within
    /// the RCU critical section.  They may not be used to modify the data
    /// (as it may be copied concurrently with the modification, and the
    /// original data element discarded).
    class iterator {
        element* _p = nullptr;
    public:
        iterator(element* p) : _p(p) {}
        iterator(const iterator&) = default;
        iterator() = default;
        /// Access the data value.
        T& operator*() const { return _p->data; }
        /// Access the data value.
        T* operator->() const { return &_p->data; }
        /// Checks whether the iterator points to a valid element.
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
    void maybe_grow(size_t new_size) {
        auto old_capacity = _buckets.read_by_owner()->size();
        if (old_capacity * 2 <= new_size) {
            resize(old_capacity * 2);
        }
    }
    void maybe_shrink(size_t new_size) {
        auto old_capacity = _buckets.read_by_owner()->size();
        if (old_capacity / 2 > new_size) {
            resize(old_capacity / 2);
        }
    }
    void resize(size_t new_size);
public:
    rcu_hashtable(const rcu_hashtable&) = delete;
    void operator=(const rcu_hashtable&) = delete;
    /// Constructs an empty hash table with the given @capacity.
    explicit rcu_hashtable(size_t capacity, Hash hash = Hash())
        : _buckets(new bucket_array_type(capacity)), _hash(hash) {}
    /// Constructs an empty hash table.
    explicit rcu_hashtable(Hash hash = Hash())
        : rcu_hashtable(1, hash) {}
    ~rcu_hashtable();

    /// Find an item using a given @key.
    ///
    /// Looks for an item in the bucket given by @key_hash(@key), and matched element e
    /// using @kvc(@key, e) == true.
    ///
    /// Must be protected by RCU, and the result must be consumed
    /// within the RCU critical section.
    template <typename Key, typename KeyHash = std::hash<Key>, typename KeyValueCompare = std::equal_to<T>>
    iterator reader_find(const Key& key, KeyHash key_hash = KeyHash(), KeyValueCompare kvc = KeyValueCompare()) {
        return find(key, key_hash, kvc, reader_traits());
    }

    /// Find an item equal to another item
    ///
    /// Must be protected by RCU, and the result must be consumed
    /// within the RCU critical section.
    iterator reader_find(const T& key) {
        return reader_find(key, _hash, std::equal_to<T>());
    }

    /// Find an item using a given @key.
    ///
    /// Looks for an item in the bucket given by @key_hash(@key), and matched element e
    /// using @kvc(@key, e) == true.
    ///
    /// Requires external mutual exclusion.
    /// Result must be consumed before the next modification.
    template <typename Key, typename KeyHash = std::hash<Key>, typename KeyValueCompare = std::equal_to<T>>
    iterator owner_find(const Key& key, KeyHash key_hash = KeyHash(), KeyValueCompare kvc = KeyValueCompare()) {
        return find(key, key_hash, kvc, owner_traits());
    }

    /// Find an item equal to another item
    ///
    /// Requires external mutual exclusion.
    /// Result must be consumed before the next modification.
    iterator owner_find(const T& key) {
        return owner_find(key, _hash, std::equal_to<T>());
    }

    /// Erase an item previously found by owner_find().
    ///
    /// Requires external mutual exclusion.
    void erase(iterator i);

    /// Inserts an item
    ///
    /// Requires external mutual exclusion.
    void insert(const T& data) {
        insert(new element(data));
    }

    /// Inserts an item, possibly destroying the source
    ///
    /// Requires external mutual exclusion.
    void insert(T&& data) {
        insert(new element(std::move(data)));
    }

    /// Constructs a new item in place.
    ///
    /// Requires external mutual exclusion.
    template <typename... Arg>
    void emplace(Arg... arg) {
        insert(new element(std::forward<Arg>(arg)...));
    }

    /// Executes a function on all items
    ///
    /// Requires external mutual exclusion.  @func may not
    /// call erase() or insert().
    template <typename Func>
    void owner_for_each(Func func) {
        for_each(func, owner_traits());
    }

    /// Executes a function on all items
    ///
    /// Must be run within an RCU read-side critical section.
    template <typename Func>
    void reader_for_each(Func func) {
        for_each(func, reader_traits());
    }

    /// Determines the number of items in the hash table
    ///
    /// Can be called without any locks, but the value is only
    /// stable if external mutual exclusion is provided.
    size_t size() const {
        return _size.load(std::memory_order_relaxed);
    }

    /// Determines whether the hash table is empty
    ///
    /// Can be called without any locks, but the value is only
    /// stable if external mutual exclusion is provided.
    bool empty() const {
        return !size();
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
    auto new_size = _size.load(std::memory_order_relaxed) + 1;
    _size.store(new_size, std::memory_order_relaxed);
    maybe_grow(new_size);
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
    auto new_size = _size.load(std::memory_order_relaxed) - 1;
    _size.store(new_size, std::memory_order_relaxed);
    auto p = i._p;
    next_ptr* next = p->next.read_by_owner();
    next_ptr* prev = p->prev;
    if (next) {
        static_cast<element*>(next)->prev = prev;
    }
    prev->next.assign(next);
    rcu_dispose(p);
    maybe_shrink(new_size);
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
