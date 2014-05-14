#ifndef PERCPU_XMIT_HH_
#define PERCPU_XMIT_HH_

#include <atomic>
#include <osv/nway_merger.hh>

#include <osv/types.h>
#include <osv/percpu.hh>
#include <osv/wait_record.hh>
#include <osv/percpu_xmit.hh>

#include <lockfree/ring.hh>
#include <lockfree/queue-mpsc.hh>

#include <osv/clock.hh>

#include <bsd/sys/sys/mbuf.h>

namespace osv {

/**
 * @class cpu_queue
 * This class will represent a single per-CPU Tx queue.
 *
 * These queues will be subject to the merging by the nway_merger class in
 * order to address the reordering issue. Therefore this class will
 * implement the following methods/classes:
 *  - push(val)
 *  - empty()
 *  - front(), which will return the iterator that implements:
 *      - operator *() to access the underlying value
 *  - erase(it), which would pop the front element.
 *
 * If the producer fails to push a new element into the queue (the queue is
 * full) then it may start "waiting for the queue": request to be woken when the
 * queue is not full anymore (when the consumer frees some some entries from the
 * queue):
 *
 *  - push_new_waiter() method.
 */
template <unsigned CpuTxqSize>
class cpu_queue {
public:
    /**
     * @struct buff_desc
     *
     * A pair of pointer to buffer and the timestamp.
     * Two objects are compared by their timestamps.
     */
    struct buff_desc {
        clock::uptime::time_point ts;
        void* cooky;

        bool operator>(const buff_desc& other) const
        {
            return ts - other.ts > 0;
        }
    };

    class cpu_queue_iterator;
    typedef cpu_queue_iterator   iterator;
    typedef buff_desc            value_type;

    explicit cpu_queue() {}

    class cpu_queue_iterator {
    public:
        void* operator*() const { return _cpuq->front().cooky; }

    private:
        typedef cpu_queue<CpuTxqSize> cpu_queue_type;

        // We want only tx_cpu_queue to be able to create such interators.
        friend cpu_queue_type;
        explicit cpu_queue_iterator(cpu_queue_type* cpuq) : _cpuq(cpuq) { }
        cpu_queue_type* _cpuq;
    };

    /**
     * Delete the item pointed by the given iterator and wake the next
     * waiter if there is any.
     *
     * Since iterator may only point to the front element we just need to
     * pop() the underlying ring_spsc.
     * @param it iterator handle
     */
    void erase(iterator& it) {
        value_type tmp;
        _r.pop(tmp);
        _popped_since_wakeup++;

        debug_check(tmp);

        //
        // Wake the waiters after a threshold or when the last packet has
        // been popped.
        // The last one is needed to ensure there won't be stuck waiters in
        // case of a race described in net::txq::push_cpu().
        //
        if (_r.empty() || (_popped_since_wakeup >= _wakeup_threshold)) {
            wake_waiters();
        }
    }

    void wake_waiters() {
        if (!_popped_since_wakeup) {
            return;
        }

        //
        // If we see the empty waiters queue we want to clear the popped
        // packets counter in order to keep the wakeup logic consistent.
        //
        if (_waitq.empty()) {
            _popped_since_wakeup = 0;
            return;
        }

        //
        // We need to ensure that woken thread will see the new state of the
        // queue (after the pop()).
        //
        std::atomic_thread_fence(std::memory_order_seq_cst);

        for (; _popped_since_wakeup; _popped_since_wakeup--) {
            // Wake the next waiter if there is any
            wait_record* wr = _waitq.pop();
            if (wr) {
                wr->wake();
            } else {
                _popped_since_wakeup = 0;
                return;
            }
        }
    }

    // Some access/info functions
    const value_type& front() const { return _r.front(); }
    iterator begin() { return iterator(this); }
    bool push(value_type v) { return _r.push(v); }
    bool empty() const { return _r.empty(); }
    void push_new_waiter(wait_record* wr) { _waitq.push(wr); }

private:
    lockfree::queue_mpsc<wait_record> _waitq;
    ring_spsc<value_type, CpuTxqSize> _r;

    //
    // We don't want to wake the waiters when the Tx worker is going to sleep.
    // We would like them to work in parallel, so half a ring should be just
    // fine as a threshold.
    //
    static const int _wakeup_threshold = CpuTxqSize / 2;
    int _popped_since_wakeup = 0;

#if defined(TX_DEBUG) && !defined(NDEBUG)
    void debug_check(value_type& tmp) {
        if (tmp.ts <= _last_ts) {
            printf("Time went backwards: curr_ts(%d) < prev_ts(%d)\n",
                   tmp.ts, _last_ts);
            assert(0);
        }

        _last_ts = tmp.ts;
    }
    s64 _last_ts = -1;
#else
    void debug_check(value_type& tmp) {}
#endif
} CACHELINE_ALIGNED;

} // namespace osv

#endif // PERCPU_XMIT_HH_
