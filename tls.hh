#ifndef TLS_HH_
#define TLS_HH_

struct thread_control_block {
    thread_control_block* self;
    void* tls_base;
};

#endif /* TLS_HH_ */
