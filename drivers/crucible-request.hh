/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CRUCIBLE_REQUEST_HH
#define CRUCIBLE_REQUEST_HH

#include "crucible-types.hh"
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include <map>

namespace crucible {

/**
 * Pending request tracker for quorum logic.
 *
 * Tracks responses from 3 downstairs servers and determines when
 * quorum (2/3) is reached.
 */
struct PendingRequest {
    uint64_t job_id;

    // Response tracking
    std::atomic<int> responses_received{0};
    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};

    // Per-downstairs state
    std::array<bool, 3> downstairs_responded{false, false, false};
    std::array<CrucibleError, 3> downstairs_errors;

    // For read operations: store response data
    std::array<std::vector<uint8_t>, 3> read_data;
    std::array<std::vector<ReadBlockContext>, 3> read_contexts;

    // Synchronization
    mutex mtx;
    condvar cv;

    // Completion state
    bool completed{false};
    Result<void> result;

    // Timestamp for timeout detection
    std::chrono::steady_clock::time_point start_time;

    // Required quorum (2 for normal ops, 3 for snapshots)
    int required_quorum{2};

    PendingRequest(uint64_t id, int quorum = 2)
        : job_id(id)
        , result(Result<void>::err(CrucibleError::Timeout))
        , start_time(std::chrono::steady_clock::now())
        , required_quorum(quorum)
    {}

    /**
     * Mark response from a downstairs server.
     *
     * @param downstairs_idx Index of downstairs (0-2)
     * @param success True if operation succeeded
     * @param error Error code if failed
     */
    void mark_response(int downstairs_idx, bool success,
                      CrucibleError error = CrucibleError::IoError);

    /**
     * Wait for quorum (2/3 responses).
     *
     * The timeout is a deadlock backstop, not a normal-operation trigger.
     * Upstream Crucible has no per-I/O timeout: it pings every 5 s and only
     * declares a downstairs faulted after 45 s of socket inactivity, then
     * waits indefinitely for a slow-but-alive op to ack.  We mirror that.  A
     * single ZFS flush at large txg sizes makes the downstairs fsync several
     * dirty 64 MiB extents; on a contended disk that easily exceeds the old
     * 5 s, and failing the flush with EIO suspends the pool (failmode=wait)
     * and wedges txg_wait_synced forever -- the 256 MiB hang.  A dead
     * downstairs is detected promptly by TCP keepalive (~25 s) which closes
     * the socket and triggers RequestManager::fail_downstairs(), so this long
     * backstop only fires on a true deadlock, never on a healthy slow op.
     *
     * @param timeout_ms Timeout in milliseconds
     * @return true if quorum reached with success, false otherwise
     */
    bool wait_for_quorum(unsigned timeout_ms = 120000);

    /**
     * Check if quorum is reached.
     *
     * @return true if required quorum responses received (success or error)
     */
    bool has_quorum() const {
        return success_count >= required_quorum || error_count >= required_quorum;
    }

    /**
     * Check if request timed out.
     *
     * @param timeout_ms Timeout threshold
     * @return true if request exceeded timeout
     */
    bool is_timed_out(unsigned timeout_ms) const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time).count();
        return elapsed >= timeout_ms;
    }
};

/**
 * Request manager for tracking pending operations.
 */
class RequestManager {
public:
    RequestManager();
    ~RequestManager();

    /**
     * Create a new pending request.
     *
     * @param job_id Job identifier
     * @param required_quorum Required number of successful responses (default 2)
     * @return Shared pointer to pending request
     */
    std::shared_ptr<PendingRequest> create_request(uint64_t job_id, int required_quorum = 2);

    /**
     * Find an existing pending request.
     *
     * @param job_id Job identifier
     * @return Shared pointer to pending request, or nullptr if not found
     */
    std::shared_ptr<PendingRequest> find_request(uint64_t job_id);

    /**
     * Remove a completed request.
     *
     * @param job_id Job identifier
     */
    void remove_request(uint64_t job_id);

    /**
     * Get count of pending requests.
     *
     * @return Number of pending requests
     */
    size_t pending_count() const;

    /**
     * Cancel all pending requests.
     */
    void cancel_all();

    /**
     * Record a downstairs failure against every in-flight request.
     *
     * Called when a downstairs socket closes.  Marks an error response from
     * that downstairs index on each pending request and re-evaluates quorum,
     * so an op that can no longer reach 2/3 (we do not implement live repair)
     * fails immediately instead of waiting out the full wait_for_quorum
     * backstop.  An op that still has 2 live downstairs keeps waiting on the
     * survivors.  Requests that already saw a response from this downstairs
     * are unaffected (mark_response is idempotent per downstairs).
     *
     * @param downstairs_idx Index of the downstairs that disconnected (0-2)
     */
    void fail_downstairs(int downstairs_idx);

private:
    mutable mutex mtx_;
    std::map<uint64_t, std::shared_ptr<PendingRequest>> requests_;
};

/**
 * Job ID allocator.
 *
 * Provides monotonically increasing job IDs for operations.
 */
class JobIdAllocator {
public:
    JobIdAllocator() : next_id_(1) {}

    /**
     * Allocate next job ID.
     *
     * @return Unique job ID
     */
    uint64_t allocate() {
        return next_id_.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * Get current ID (for debugging).
     *
     * @return Current job ID value
     */
    uint64_t current() const {
        return next_id_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> next_id_;
};

} // namespace crucible

#endif // CRUCIBLE_REQUEST_HH
