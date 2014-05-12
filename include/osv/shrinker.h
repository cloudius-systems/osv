#ifndef SHRINKER_H_
#define SHRINKER_H_

#ifdef __cplusplus

#include <osv/mempool.hh>

class c_shrinker : public memory::shrinker {
public:
    explicit c_shrinker(const char *name,
                        size_t (*func)(size_t target, bool hard)) :
            memory::shrinker(name), _func(func) {}
    size_t request_memory(size_t s, bool hard) { return _func(s, hard); }
private:
    size_t (*_func)(size_t target, bool hard);
};


extern "C"
void *osv_register_shrinker(const char *name,
                            size_t (*func)(size_t target, bool hard));
#else
#include <stdbool.h>
void *osv_register_shrinker(const char *name,
                            size_t (*func)(size_t target, bool hard));
#endif

#endif
