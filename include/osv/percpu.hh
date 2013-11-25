/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PERCPU_HH_
#define PERCPU_HH_

#include <sched.hh>
#include <type_traits>
#include <memory>

extern __thread char* percpu_base;

template <typename T>
class percpu {
public:
    // We need percpu<T> variables to be defined with the PERCPU macro so
    // they'll end up in the right section. To enforce this, we add a fake
    // argument to the constructor. The type percpu<T> still needs to be
    // used to declare the variable separately from its definition (which
    // is needed for class static variables).
    static constexpr struct
        please_use_PERCPU_macro {} please_use_PERCPU_macro {};
    // percpu<T>'s constructor needs to be constexpr so that the .percpu
    // section can be constructed at compile time, and then at early run time
    // be copied to per-cpu copies of this section, without risking that the
    // constructor hasn't run yet.
    explicit constexpr percpu(struct please_use_PERCPU_macro) { }
    // You can't copy a per-cpu variable and get a new per-cpu variable.
    // Neither can one be moved (its address is important).
    percpu(const percpu&) = delete;
    percpu(percpu&&) = delete;

    T* operator->() {
        return addr();
    }
    T& operator*() {
        return *addr();
    }
    T* for_cpu(sched::cpu* cpu) {
        return addr(cpu->percpu_base);
    }
private:
    T *addr(char* base = percpu_base) {
        size_t offset = reinterpret_cast<size_t>(&_var);
        return reinterpret_cast<T*>(base + offset);
    }
private:
    T _var;
    friend size_t dynamic_percpu_base();
};

#define PERCPU(type, var) __attribute__((section(".percpu"))) \
            percpu<type> var (percpu<type>::please_use_PERCPU_macro)

size_t dynamic_percpu_alloc(size_t size, size_t align);
void dynamic_percpu_free(size_t offset, size_t size);

template <typename T, size_t align = std::alignment_of<T>::value>
class dynamic_percpu {
public:
    dynamic_percpu()
        : _offset(dynamic_percpu_alloc(sizeof(T), align))
        // we want a lambda instead of boost::bind(), but this mysteriously fails in gcc 4.7.2
        , _notifier(new sched::cpu::notifier(std::bind(&dynamic_percpu::construct, this)))
    {
        for (auto c : sched::cpus) {
            new (for_cpu(c)) T();
        }
    }
    ~dynamic_percpu() {
        for (auto c : sched::cpus) {
            for_cpu(c)->~T();
        }
        dynamic_percpu_free(_offset, sizeof(T));
    }
    T* operator->() { return addr(); }
    T& operator*() { return *addr(); }
    T* for_cpu(sched::cpu* cpu) { return addr(cpu->percpu_base); }
private:
    void construct() {
        new (addr()) T();
    }
    T* addr(void* base = percpu_base) {
        return static_cast<T*>(base + _offset);
    }
private:
    size_t _offset;
    std::unique_ptr<sched::cpu::notifier> _notifier;
};


template <typename T>
struct autoconstructed_ptr : std::unique_ptr<T> {
    autoconstructed_ptr() : std::unique_ptr<T>(new T) {}
};

template <typename T>
using dynamic_percpu_indirect = dynamic_percpu<autoconstructed_ptr<T>>;

void percpu_init(unsigned cpu);

#endif /* PERCPU_HH_ */
