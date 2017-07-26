/*
 * Copyright (C) 2017 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ALIGNED_NEW_HH
#define ALIGNED_NEW_HH

/**
 * In C++11, "new T()" is not guaranteed to fulfill unusual alignment
 * requirements that T might have. The problem is that there is no way to
 * tell "operator new" (which "new T()" calls to allocate memory) to use
 * a particular alignment. C++17 fixed both oversights, but we cannot use
 * C++17 yet in OSv which only assumes C++11.
 * So our workaround is a template aligned_new<T> which behaves like a
 * "new" expression, but uses the C11 aligned_alloc() instead of C++'s
 * operator new to allocate memory. We assume, and this is currently the
 * case in OSv, that operator new and malloc() do the same thing anyway
 * and it is legal to call "operator delete" on memory returned by
 * aligned_alloc().
 *
 * In the future, when C++17 becomes common, we can switch to using ordinary
 * new instead of aligned_new.
 */

template<typename T, typename... Args>
T* aligned_new(Args&&... args) {
    void *p = aligned_alloc(alignof(T), sizeof(T));
    assert(p);
    return new(p) T(std::forward<Args>(args)...);
}

// Similar function for allocating an array of objects. But here we have
// a problem: While an object created with aligned_new<>() can be deleted by
// an ordinary "delete" (as explained above), here, an array allocated by an
// aligned_array_new() CANNOT be deleted by a "delete[]" expression! This is
// because we do not know the internal layout of "new[]" of this compiler
// to allow a delete[] to work out of the box (delete[] needs to be able
// to figure out the number of individual objects in the array which it
// needs to destruct). So we have a separate aligned_array_delete().
template<typename T>
T* aligned_array_new(size_t len) {
    // Allocate one extra item in the beginning, for length.
    static_assert(sizeof(T) > sizeof(size_t), "small T in aligned_array_new");
    void *p = aligned_alloc(alignof(T), sizeof(T) * (len+1));
    assert(p);
    *(size_t *)p = len;
    T* ret = (T*) (p + sizeof(T));
    for (unsigned i = 0; i < len; i++) {
        p += sizeof(T);
        new(p) T();
    }
    return ret;
} 

template<typename T>
void aligned_array_delete(T* p) {
    static_assert(sizeof(T) > sizeof(size_t), "small T in aligned_array_new");
    size_t len = *(size_t*)p;
    for (unsigned i = 0; i < len ; i++) {
        ++p;
        *p->~T();
    }
    free(p);
} 
#endif /* ALIGNED_NEW_HH */
