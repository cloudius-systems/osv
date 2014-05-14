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

/**
 * @class xmitter
 *
 * This class is a heart of a per-CPU Tx framework.
 * Except for a constructor it has two public methods:
 *  - xmit(buff): push the packet descriptor downstream either to the HW or into
 *    the per-CPU queue if there is a contention.
 *
 *  - poll_until(cond): this is a main function of a worker thread that will
 *    consume packet descriptors from the per-CPU queue(s) and send them to
 *    the output iterator (which is responsible to ensure their successful
 *    sending to the HW channel).
 */
template <class NetDevTxq, unsigned CpuTxqSize>
class xmitter {
public:
    explicit xmitter(NetDevTxq* txq) :
        _txq(txq),_check_empty_queues(false) {
        for (auto c : sched::cpus) {
            _cpuq.for_cpu(c)->reset(new cpu_queue_type);
        }
    }

    /**
     * A main transmit function: will try to bypass the per-CPU queue if
     * possible and will push the frame into that queue otherwise.
     *
     * It may only block if it needs to push the frame into the per-CPU queue
     * and it's full.
     *
     * @param buff packet descriptor to send
     *
     * @return 0 in case of success, EINVAL if a packet is not well-formed.
     */
    int xmit(mbuf* buff) {

        void* cooky = nullptr;
        int rc = _txq->xmit_prep(buff, cooky);

        if (rc == EINVAL) {
            m_freem(buff);
            return rc;
        }

        assert(cooky != nullptr);

        //
        // If there are pending packets (in the per-CPU queues) or we've failed
        // to take a RUNNING lock push the packet in the per-CPU queue.
        //
        // Otherwise means that a dispatcher is neither running nor is
        // scheduled to run. In this case bypass per-CPU queues and transmit
        // in-place.
        //
        if (has_pending() || !try_lock_running()) {
            push_cpu(cooky);
            return 0;
        }

        // If we are here means we've aquired a RUNNING lock
        rc = _txq->try_xmit_one_locked(cooky);

        // Alright!!!
        if (!rc) {
            if (_txq->kick_hw()) {
            }
        }

        unlock_running();

        //
        // We unlock_running() not from a dispatcher only if the dispatcher is
        // not running and is waiting for either a new work or for this lock.
        //
        // We want to wake a dispatcher only if there is a new work for it since
        // otherwise there is no point for it to wake up.
        //
        if (has_pending()) {
            _txq->wake_worker();
        }

        if (rc /* == ENOBUFS */) {
            //
            // There hasn't been enough buffers on the HW ring to send the
            // packet - push it into the per-CPU queue, dispatcher will handle
            // it later.
            //
            push_cpu(cooky);
        }

        return 0;
    }

    template <class StopPollingPred, class XmitIterator>
    void poll_until(StopPollingPred stop_pred, XmitIterator& xmit_it) {
        // Create a collection of a per-CPU queues
        std::list<cpu_queue_type*> all_cpuqs;

        for (auto c : sched::cpus) {
            all_cpuqs.push_back(_cpuq.for_cpu(c)->get());
        }

        // Push them all into the heap
        _mg.create_heap(all_cpuqs);

        //
        // Dispatcher holds the RUNNING lock all the time it doesn't sleep
        // waiting for a new work.
        //
        lock_running();

        // Start taking packets one-by-one and send them out
        while (!stop_pred()) {
            //
            // Reset the PENDING state.
            //
            // The producer thread will first add a new element to the heap and
            // only then set the PENDING state.
            //
            // We need to ensure that PENDING is cleared before _mg.pop() is
            // performed (and possibly returns false - the heap is empty)
            // because otherwise the producer may see the "old" value of the
            // PENDING state and won't wake us up.
            //
            clear_pending();

            // Check if there are elements in the heap
            if (!_mg.pop(xmit_it)) {
                // Wake all unwoken waiters before going to sleep
                wake_waiters_all();

                // We are going to sleep - release the HW channel
                unlock_running();

                sched::thread::wait_until([this] { return has_pending(); });

                lock_running();
            }

            while (_mg.pop(xmit_it)) {
                _txq->kick_pending_with_thresh();
            }

            // Kick any pending work
            _txq->kick_pending();
        }

        // TODO: Add some handshake like a bool variable here
        assert(0);
    }

private:
    void wake_waiters_all() {
        for (auto c : sched::cpus) {
            _cpuq.for_cpu(c)->get()->wake_waiters();
        }
    }

    /**
     * Push the packet into the per-CPU queue for the current CPU.
     * @param buf packet descriptor to push
     */
    void push_cpu(void* cooky) {
        bool success = false;

        sched::preempt_disable();

        cpu_queue_type* local_cpuq = _cpuq->get();
        typename cpu_queue_type::value_type new_buff_desc = { get_ts(), cooky };

        while (!local_cpuq->push(new_buff_desc)) {
            wait_record wr(sched::thread::current());
            local_cpuq->push_new_waiter(&wr);

            //
            // Try to push again in order to resolve a nasty race:
            //
            // If dispatcher has succeeded to empty the whole ring before we
            // added our record to the waitq then without this push() we could
            // have stuck until somebody else adds another packet to this
            // specific cpuq. In this case adding a packet will ensure that
            // dispatcher eventually handles it and "wake()s" us up.
            //
            // If we fail to add the packet here then this means that the queue
            // has still been full AFTER we added the wait_record and we need to
            // wait until dispatcher cleans it up and wakes us.
            //
            // All this is because we can't exit this function until dispatcher
            // pop()s our wait_record since it's allocated on our stack.
            //
            success = local_cpuq->push(new_buff_desc);
            if (success && !test_and_set_pending()) {
                _txq->wake_worker();
            }

            sched::preempt_enable();

            wr.wait();

            // we are done - get out!
            if (success) {
                return;
            }

            sched::preempt_disable();

            // Refresh: we could have been moved to a different CPU
            local_cpuq = _cpuq->get();
            //
            // Refresh: another thread could have pushed its packet before us
            //          and i t had an earlier timestamp - we have to keep the
            //          timestampes ordered in the CPU queue.
            //
            new_buff_desc.ts = get_ts();
        }

        //
        // Try to save the IPI sending (when dispatcher sleeps for an interrupt)
        // and exchange in the wake_impl() by paying a price of an exchange
        // operation here.
        //
        if (!test_and_set_pending()) {
            _txq->wake_worker();
        }

        sched::preempt_enable();
    }

    /**
     * @return the current timestamp
     */
    clock::uptime::time_point get_ts() {
        return clock::uptime::now();

    }

    // RUNNING state controling functions
    bool try_lock_running() {
        return !_running.test_and_set(std::memory_order_acquire);
    }

    void lock_running() {
        //
        // Check if there is no fast-transmit hook running already.
        // If yes - sleep until it ends.
        //
        if (!try_lock_running()) {
            sched::thread::wait_until([this] { return try_lock_running(); });
        }
    }
    void unlock_running() {
        _running.clear(std::memory_order_release);
    }

    // PENDING (packets) controling functions
    bool has_pending() const {
        return _check_empty_queues.load(std::memory_order_acquire);
    }

    bool test_and_set_pending() {
        return _check_empty_queues.exchange(true, std::memory_order_acq_rel);
    }
    void clear_pending() {
        _check_empty_queues.store(false, std::memory_order_release);
    }

private:
    typedef cpu_queue<CpuTxqSize> cpu_queue_type;

    NetDevTxq* _txq; // Rename to _dev_txq
    dynamic_percpu<std::unique_ptr<cpu_queue_type> > _cpuq;
    osv::nway_merger<std::list<cpu_queue_type*> >      _mg    CACHELINE_ALIGNED;
    std::atomic<bool>                  _check_empty_queues    CACHELINE_ALIGNED;
    //
    // This lock will be used to get an exclusive control over the HW
    // channel.
    //
    std::atomic_flag                              _running    CACHELINE_ALIGNED
                                                           = ATOMIC_FLAG_INIT;
};

} // namespace osv

#endif // PERCPU_XMIT_HH_
