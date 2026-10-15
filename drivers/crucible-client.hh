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
#include "crucible-messages.hh"
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
 * Thread-safety: concurrent operations and disconnect are supported.
 * Destruction requires external callers quiesced; a stopped client cannot reconnect.
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
                  bool encrypted = false,
                  uint64_t generation = 0);

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
    uint64_t generation_;
    uint64_t flush_number_{0};  // Incremented on each flush
    bool read_only_;
    bool encrypted_;

    // Connections (3 downstairs servers)
    std::array<std::unique_ptr<Connection>, 3> connections_;
    std::atomic<int> connected_count_{0};
    std::array<std::atomic<bool>, 3> admitted_;
    std::array<ExtentVersions, 3> versions_;
    mutex lifecycle_mtx_;
    std::atomic<bool> stopping_{false};
    void disconnect_locked();
    bool attempted_{false}; // A session cannot be restarted without repair/admission.
    struct ReceiveState {
        std::vector<uint8_t> bytes;
        size_t wanted{4};
        std::chrono::steady_clock::time_point started;
    };
    std::array<ReceiveState, 3> receive_;
    void fail_downstairs(int index);
    bool receive_available(int index, std::vector<uint8_t>& frame);
    void validate_cohort();

    // Independent bounded sender queues keep a lagging replica from blocking
    // dispatch to the healthy quorum. Wire dependencies preserve job ordering.
    struct SendFrame {
        std::vector<uint8_t> header;  // frame prefix + bincode header (+ data len)
        std::vector<uint8_t> data;    // bulk payload for Writes; empty otherwise
    };
    struct DownstairsSender {
        mutex mtx;
        condvar cv;
        std::deque<SendFrame> queue;
        size_t queued_bytes{0};
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
     * wire as one contiguous Crucible frame).  A send failure permanently
     * quarantines this replica; no reconnect is attempted.
     */
    void sender_loop(int downstairs_idx);

    /** Start the three sender threads (called from connect()). */
    void start_senders();

    /** Stop and join the sender threads, draining queues (called from disconnect()). */
    void stop_senders();

    // Request tracking
    JobIdAllocator job_allocator_;
    RequestManager request_mgr_;

    // ponytail: serialized operations form a transitive last-job chain,
    // including reads and flushes. Range tracking can restore parallelism.
    mutex operation_mtx_;
    uint64_t last_job_id_{0};
    std::vector<uint64_t> begin_job(uint64_t& job_id);
    void fence_session();

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
    void process_responses(int downstairs_idx, const std::vector<uint8_t>& frame);

    /**
     * Perform handshake with a downstairs server.
     *
     * @param downstairs_idx Index of downstairs (0-2)
     * @throws std::runtime_error on handshake failure
     */
    void handshake(int downstairs_idx, std::chrono::steady_clock::time_point deadline);

    /**
     * Query region information from downstairs.
     *
     * @param downstairs_idx Index of downstairs (0-2)
     * @throws std::runtime_error on query failure
     */
    void query_region_info(int downstairs_idx, std::chrono::steady_clock::time_point deadline);

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
