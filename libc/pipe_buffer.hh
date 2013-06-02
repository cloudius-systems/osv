#ifndef PIPE_BUFFER_HH_
#define PIPE_BUFFER_HH_

#include <deque>
#include <atomic>
#include <boost/intrusive_ptr.hpp>

#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/file.h>

struct pipe_buffer {
private:
    static constexpr size_t max_buf = 8192;
public:
    pipe_buffer() = default;
    pipe_buffer(const pipe_buffer&) = delete;
    int read(uio* data);
    int write(uio* data);
    int read_events();
    int write_events();
    void detach_sender();
    void detach_receiver();
    void attach_sender(struct file *f);
    void attach_receiver(struct file *f);
private:
    int read_events_unlocked();
    int write_events_unlocked();
private:
    mutex mtx;
    std::deque<char> q;
    struct file *receiver = nullptr;
    struct file *sender = nullptr;
    std::atomic<unsigned> refs = {};
    condvar may_read;
    condvar may_write;
    friend void intrusive_ptr_add_ref(pipe_buffer* p) {
        p->refs.fetch_add(1, std::memory_order_relaxed);
    }
    friend void intrusive_ptr_release(pipe_buffer* p) {
        if (p->refs.fetch_add(-1, std::memory_order_acquire) == 1) {
            delete p;
        }
    }
};

typedef boost::intrusive_ptr<pipe_buffer> pipe_buffer_ref;

#endif /* PIPE_BUFFER_HH */
