/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "osv/nway_merger.hh"
#include <iostream>
#include <iterator>
#include <vector>
#include <list>
#include <array>
#include <typeinfo>
//#include <boost/lockfree/spsc_queue.hpp>
#include "lockfree/ring.hh"

using namespace std;

/**
 * We will merge containers with this objects
 */
struct my_struct {
    my_struct() {}
    //my_struct(const my_struct& c) : cookie(c.cookie), key(c.key) {}
    my_struct(unsigned long c, signed int i) : cookie(c), key(i) { }
    int cookie;
    signed int key;
    bool operator>(const my_struct& other) const
    {
        return key > other.key;
    }

    friend ostream& operator<<(ostream& os, const my_struct& c)
    {
        os<<"("<<c.cookie<<","<<c.key<<")";
        return os;
    }
};

template <class T, unsigned MaxSize>
class my_spsc_ring {
public:

    class my_spsc_ring_iterator {
    public:
        const T& operator *() const { return _r->front(); }

    private:
        friend class my_spsc_ring;
        explicit my_spsc_ring_iterator(my_spsc_ring<T, MaxSize>* r) : _r(r) { }
        my_spsc_ring<T, MaxSize>* _r;
    };

    typedef my_spsc_ring_iterator      iterator;

    explicit my_spsc_ring() {}

    const T& front() const { return _r.front(); }
    iterator begin() { return iterator(this); }

    bool push(T v) { return _r.push(v); }

    void erase(iterator &it) {
        T tmp;
        _r.pop(tmp);
    }

    bool empty() const { return _r.empty(); }

private:
    ring_spsc<T, MaxSize> _r;
};

typedef my_spsc_ring<my_struct, 8> my_spsc_queue;

static void fill_abc(my_spsc_queue& a, my_spsc_queue& b, my_spsc_queue& c)
{
    a.push(my_struct(123, 1));
    a.push(my_struct(123, 3));
    a.push(my_struct(123, 5));
    a.push(my_struct(123, 9));
    a.push(my_struct(123, 10));

    b.push(my_struct(321, 2));
    b.push(my_struct(321, 6));
    b.push(my_struct(321, 7));
    b.push(my_struct(321, 13));

    c.push(my_struct(312, 4));
    c.push(my_struct(312, 6));
    c.push(my_struct(312, 8));
    c.push(my_struct(312, 11));
    c.push(my_struct(312, 12));
    c.push(my_struct(312, 14));
}

template <class C>
static void fill_abc(C& a, C& b, C& c)
{
    a.emplace_back(123, 1);
    a.emplace_back(123, 3);
    a.emplace_back(123, 5);
    a.emplace_back(123, 9);
    a.emplace_back(123, 10);

    b.emplace_back(321, 2);
    b.emplace_back(321, 6);
    b.emplace_back(321, 7);
    b.emplace_back(321, 13);

    c.emplace_back(312, 4);
    c.emplace_back(312, 6);
    c.emplace_back(312, 8);
    c.emplace_back(312, 11);
    c.emplace_back(312, 12);
    c.emplace_back(312, 14);
}

template <class C>
static bool test_nway_merge_step(C& sl, osv::nway_merger<C>& m,
                                 list<my_struct>& dest)
{
    auto it = sl.begin();
    auto a = *it++, b = *it++, c = *it;

    fill_abc(*a, *b, *c);

    m.merge(sl, back_inserter(dest));

    if (dest.empty()) {
        cerr<<"Hmmm... merge result is empty! FAIL"<<endl;
        return false;
    }

    auto t = dest.front();
    dest.pop_front();

    cout<<t<<" ";

    for (auto c : dest) {
        if (t > c) {
            cerr<<"nway_merger is broken! FAIL"<<endl;
            return false;
        }

        t = c;

        cout<<t<<" ";
    }

    cout<<endl;

    dest.clear();

    return true;
}

template <class C>
static bool test_nway_merge(C& sl, list<my_struct>& dest)
{
    osv::nway_merger<C> m;

    // Run 1
    if (!test_nway_merge_step(sl, m, dest)) {
        return false;
    }

    // Run 2: check that the repeated invocation of merge() works
    if (!test_nway_merge_step(sl, m, dest)) {
        return false;
    }


    cout<<"("<<typeid(C).name()<<") nway_merge tesing is ok"<<endl;
    return true;
}

bool test_list(list<my_struct>& dest)
{
    list<list<my_struct>*> sl;
    list<my_struct> a, b, c;

    sl.push_back(&a);
    sl.push_back(&b);
    sl.push_back(&c);

    if (!test_nway_merge(sl, dest)) {
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    list<my_struct> dest;
    // Test lists as both sorted sequence and as a collection container
    if (!test_list(dest))
        return 1;

    vector<vector<my_struct>*> sv;
    vector<my_struct> a, b, c;

    sv.push_back(&a);
    sv.push_back(&b);
    sv.push_back(&c);


    // Test vectors as both sorted sequence and as a collection container
    if (!test_nway_merge(sv, dest)) {
        return 1;
    }

    array<vector<my_struct>*, 3> sa;

    sa[0] = &a;
    sa[1] = &b;
    sa[2] = &c;

    // Test a vector as sorted sequence and an array as a collection container
    if (!test_nway_merge(sa, dest)) {
        return 1;
    }

    { // ring_spsc test
        my_spsc_queue a, b, c;
        array<my_spsc_queue*, 3> sa;

        sa[0] = &a;
        sa[1] = &b;
        sa[2] = &c;

        if (!test_nway_merge(sa, dest)) {
            return 1;
        }
    }

    return 0;
}
