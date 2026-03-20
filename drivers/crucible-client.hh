/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CRUCIBLE_CLIENT_HH
#define CRUCIBLE_CLIENT_HH

#include "crucible-connection.hh"
#include "crucible-types.hh"
#include "crucible-request.hh"
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <deque>

namespace crucible {

/**
 * Crucible upstairs client.
 *
 * Implements the Crucible upstairs protocol for distributed block storage.
 * Connects to 3 downstairs servers and implements 2/3 quorum logic.
 *
 * Thread-safety: Safe for concurrent operations from multiple threads.
 */
class UpsairsClient {
public:
    /**
     * Create an upstairs client.
     *
     * @param targets Vector of "host:port" strings for downstairs servers
     * @param region_uuid UUID of the region
     * @param block_size Block size in bytes (e.g., 512, 4096)
     * @param total_blocks Total number of blocks in volume
     * @param read_only Open in read-only mode
     * @param encrypted Expect encrypted blocks
     */
    UpsairsClient(const std::vector<std::string>& targets,
                  const Uuid& region_uuid,
                  uint32_t block_size,
                  uint64_t total_blocks,
                  bool read_only = false,
                  bool encrypted = false);

    ~UpsairsClient();

    // Non-copyable, non-movable
    UpsairsClient(const UpsairsClient&) = delete;
    UpsairsClient& operator=(const UpsairsClient&) = delete;
    UpsairsClient(UpsairsClient&&) = delete;
    UpsairsClient& operator=(UpsairsClient&&) = delete;

    /**
     * Connect to downstairs servers and perform handshake.
     *
     * @throws ConnectionError on connection failure
     * @throws std::runtime_error on protocol error
     */
    void connect();

    /**
     * Disconnect from downstairs servers.
     */
    void disconnect();

    /**
     * Check if connected to downstairs servers.
     *
     * @return true if at least 2/3 downstairs connected
     */
    bool is_connected() const;

    /**
     * Synchronous read operation.
     *
     * @param offset Byte offset
     * @param length Byte length
     * @param buffer Buffer to read into
     * @return 0 on success, error code on failure
     */
    int read_sync(uint64_t offset, uint32_t length, void* buffer);

    /**
     * Synchronous write operation.
     *
     * @param offset Byte offset
     * @param length Byte length
     * @param buffer Buffer to write from
     * @return 0 on success, error code on failure
     */
    int write_sync(uint64_t offset, uint32_t length, const void* buffer);

    /**
     * Synchronous flush operation.
     *
     * @return 0 on success, error code on failure
     */
    int flush_sync();

    /**
     * Synchronous flush with snapshot creation.
     *
     * Creates a snapshot as part of the flush operation. Requires 3/3 quorum.
     *
     * @param snapshot_id Snapshot identifier (numeric)
     * @return 0 on success, error code on failure
     */
    int create_snapshot(uint64_t snapshot_id);

    /**
     * Synchronous discard (trim) operation.
     *
     * Discards/deallocates blocks in the given range. Used for TRIM/UNMAP.
     *
     * @param offset Byte offset (must be block-aligned)
     * @param length Length in bytes (must be block-aligned)
     * @return 0 on success, error code on failure
     */
    int discard_sync(uint64_t offset, uint64_t length);

    /**
     * Get block size.
     *
     * @return Block size in bytes
     */
    uint32_t block_size() const { return block_size_; }

    /**
     * Get total size.
     *
     * @return Total size in bytes
     */
    uint64_t total_size() const { return total_blocks_ * block_size_; }

    /**
     * Get region information.
     *
     * @return Region definition
     */
    const RegionDefinition& region_info() const { return region_def_; }

private:
    // Configuration
    std::vector<std::string> targets_;
    Uuid region_uuid_;
    Uuid upstairs_id_;        // Generated on first connection
    Uuid session_id_;         // Generated per session
    uint32_t block_size_;
    uint64_t total_blocks_;
    uint64_t generation_{0};
    uint64_t flush_number_{0};  // Incremented on each flush
    bool read_only_;
    bool encrypted_;

    // Connections (3 downstairs servers)
    std::array<std::unique_ptr<Connection>, 3> connections_;
    std::atomic<int> connected_count_{0};

    /*
     * Per-downstairs asynchronous sender.
     *
     * The decisive fix for the 256-MiB hang: the old code sent a Write/Flush
     * to downstairs 0, then 1, then 2 in a serial blocking loop, each call
     * holding that connection's send mutex across both the header and data
     * halves of the frame.  When one downstairs backpressured TCP (busy
     * fsync'ing dirty 64-MiB extents during a flush) the issuing thread
     * wedged in send()/sbwait at index 0 and NEVER advanced to downstairs 1
     * and 2 -- so the 2-of-3 quorum that the two fast replicas could have
     * satisfied was never even attempted.  wait_for_quorum then timed out,
     * the op returned EIO, ZFS failmode=wait suspended the pool, and
     * txg_wait_synced hung forever.
     *
     * Each downstairs now owns a dedicated sender thread draining a FIFO
     * frame queue.  write_sync/flush_sync/read_sync enqueue the encoded
     * frame to all connected downstairs (a non-blocking push under a short
     * mutex) and proceed straight to wait_for_quorum.  A backpressured
     * downstairs stalls only its own sender thread; the other two drain and
     * ack, quorum is reached, and the op completes.  The slow replica's
     * queue drains in submission order once its socket clears, so the
     * Write->Flush dependency lists (which encode allocation order, not wire
     * order) keep every replica consistent.  This mirrors upstream Crucible's
     * per-client async send model.
     */
    struct SendFrame {
        std::vector<uint8_t> header;  // frame prefix + bincode header (+ data len)
        std::vector<uint8_t> data;    // bulk payload for Writes; empty otherwise
    };
    struct DownstairsSender {
        mutex mtx;
        condvar cv;
        std::deque<SendFrame> queue;
        sched::thread* thread{nullptr};
        bool running{false};
    };
    std::array<DownstairsSender, 3> senders_;

    /**
     * Enqueue an already-encoded frame to a downstairs sender queue.
     *
     * Non-blocking: takes the sender mutex only long enough to push the
     * frame and wake the sender thread.  Returns false if the downstairs is
     * not connected (caller counts this toward the reachable total).
     */
    bool enqueue_frame(int downstairs_idx, std::vector<uint8_t> header,
                       std::vector<uint8_t> data = {});

    /**
     * Sender-thread main loop for one downstairs.
     *
     * Drains the FIFO queue, writing each frame (header then optional data,
     * back to back under the connection send mutex so the pair reaches the
     * wire as one contiguous Crucible frame).  A send failure closes the
     * connection; io_loop's reconnect path takes over.
     */
    void sender_loop(int downstairs_idx);

    /** Start the three sender threads (called from connect()). */
    void start_senders();

    /** Stop and join the sender threads, draining queues (called from disconnect()). */
    void stop_senders();

    // Request tracking
    JobIdAllocator job_allocator_;
    RequestManager request_mgr_;

    /*
     * Write->Flush dependency tracking.
     *
     * A Crucible downstairs applies jobs from an internal work queue and is
     * free to reorder independent jobs; the dependency list is what forces
     * ordering.  ZFS issues many concurrent Writes from its z_wr threads and
     * then a single Flush to close the txg.  Each Flush must depend on every
     * Write submitted since the previous Flush, otherwise a downstairs that
     * is lagging (e.g. the 3rd replica past 2/3 quorum) may apply the Flush
     * before an outstanding Write -- the Write then lands after the flush
     * boundary on that replica and is not durable on crash, which presents as
     * a wedged txg_wait_synced or silent divergence between replicas.
     *
     * A new Write in turn depends on the most recent Flush so a write issued
     * after a flush is ordered after it, matching the upstream upstairs.
     *
     * We deliberately do NOT add Write->Write dependencies for overlapping
     * block ranges (which the full upstream upstairs does).  ZFS is copy-on-
     * write: it never issues two concurrent writes to the same block within a
     * txg -- each logical block gets a freshly allocated DVA -- so independent
     * Writes are genuinely order-insensitive and only the Flush barrier needs
     * ordering.  The few in-place rewrites (labels, uberblock array) are
     * issued at txg-sync time, serialized behind the data Writes by this same
     * Flush barrier.
     *
     * dep_mtx_ serializes "allocate id + record" against "snapshot + reset"
     * so the snapshot taken by a Flush is exactly the set of Writes ZFS
     * ordered before it.
     */
    mutex dep_mtx_;
    std::vector<uint64_t> pending_writes_;  // Writes since last flush
    uint64_t last_flush_id_{0};             // 0 = no flush issued yet

    // I/O thread
    sched::thread* io_thread_{nullptr};
    std::atomic<bool> running_{false};

    // Region information (from handshake)
    RegionDefinition region_def_;

    // Private methods

    /**
     * Parse "host:port" string.
     *
     * @param target Target string
     * @return Pair of (host, port)
     */
    std::pair<std::string, uint16_t> parse_target(const std::string& target);

    /**
     * Allocate a job id for a Write and compute its dependency list.
     *
     * Records the write in pending_writes_ immediately, in id-allocation
     * order, so every Flush allocated afterwards lists it as a dependency.
     * The write itself depends on the most recent Flush (if any) so a write
     * issued after a flush is applied after it.  Recording at allocation (not
     * after quorum) preserves the allocation-order == watermark-order
     * invariant the downstairs CompletedJobs watermark requires.
     *
     * @param job_id Output: the allocated job id
     * @return Dependency list to place in the Write message
     */
    std::vector<uint64_t> begin_write(uint64_t& job_id);

    /**
     * Remove a write from the pending set after it failed to reach quorum.
     *
     * A failed write returns EIO (fatal to the ZFS txg); dropping it keeps a
     * later Flush from listing a write id no downstairs durably accepted.  A
     * no-op if a Flush already snapshotted and cleared the pending set.
     *
     * @param job_id The write's job id, from begin_write()
     */
    void abort_write(uint64_t job_id);

    /**
     * Allocate a job id for a Flush and compute its dependency list.
     *
     * Snapshots every Write submitted since the previous Flush (plus the
     * previous Flush itself) as dependencies, then advances the barrier at
     * allocation: clears the pending set and records this flush as the most
     * recent.  This is the barrier that makes ZFS's flush-after-writes
     * ordering hold on every replica.  The advance happens here, not in a
     * post-quorum commit, so the allocation-order invariant holds.
     *
     * @param job_id Output: the allocated job id
     * @return Dependency list to place in the Flush message
     */
    std::vector<uint64_t> begin_flush(uint64_t& job_id);

    /**
     * I/O thread main loop.
     */
    void io_loop();

    /**
     * Send a Ruok keepalive to every connected downstairs.
     *
     * Called periodically from io_loop() to keep links alive within the
     * downstairs 45 s inactivity timeout during idle ZFS phases.
     */
    void send_keepalive();

    /**
     * Process responses from a downstairs server.
     *
     * @param downstairs_idx Index of downstairs (0-2)
     */
    void process_responses(int downstairs_idx);

    /**
     * Perform handshake with a downstairs server.
     *
     * @param downstairs_idx Index of downstairs (0-2)
     * @throws std::runtime_error on handshake failure
     */
    void handshake(int downstairs_idx);

    /**
     * Query region information from downstairs.
     *
     * @param downstairs_idx Index of downstairs (0-2)
     * @throws std::runtime_error on query failure
     */
    void query_region_info(int downstairs_idx);

    /**
     * Receive a frame (length prefix + data).
     *
     * @param downstairs_idx Index of downstairs (0-2)
     * @return Received data
     */
    std::vector<uint8_t> receive_frame(int downstairs_idx);
};

/**
 * Generate a random UUID.
 *
 * @return Random UUID
 */
Uuid generate_uuid();

/**
 * Parse "host:port" string.
 *
 * @param target Target string
 * @return Pair of (host, port)
 * @throws std::runtime_error on parse error
 */
std::pair<std::string, uint16_t> parse_target_string(const std::string& target);

} // namespace crucible

#endif // CRUCIBLE_CLIENT_HH
