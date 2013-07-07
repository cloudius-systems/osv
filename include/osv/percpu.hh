#ifndef PERCPU_HH_
#define PERCPU_HH_

#include <sched.hh>

extern char _percpu_start[];

extern __thread void* percpu_base;

template <typename T>
class percpu {
public:
    constexpr percpu() {}
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
    T *addr(void* base = percpu_base) {
        size_t offset = reinterpret_cast<char*>(&_var) - _percpu_start;
        return reinterpret_cast<T*>(base + offset);
    }
private:
    T _var;
};

#define PERCPU(type, var) percpu<type> var __attribute__((section(".percpu")))

void percpu_init(unsigned cpu);

#endif /* PERCPU_HH_ */
