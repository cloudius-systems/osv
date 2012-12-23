#ifndef MUTEX_HH
#define MUTEX_HH

class mutex {
public:
    void lock();
    bool try_lock();
    void unlock();
};

#endif
