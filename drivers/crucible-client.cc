/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "crucible-client.hh"
#include "crucible-messages.hh"
#include "crucible-hash.hh"

#include <osv/sched.hh>
#include <osv/debug.h>

#include <random>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <sys/select.h>
#include <errno.h>

// OSv uses kprintf for debug logging
extern "C" {
    int kprintf(const char* fmt, ...);
}

using namespace crucible;

namespace crucible {

// Helper: Generate random UUID
Uuid generate_uuid()
{
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint8_t> dis(0, 255);

    Uuid uuid;
    for (int i = 0; i < 16; i++) {
        uuid.bytes[i] = dis(gen);
    }

    // Set version (4) and variant bits
    uuid.bytes[6] = (uuid.bytes[6] & 0x0F) | 0x40;  // Version 4
    uuid.bytes[8] = (uuid.bytes[8] & 0x3F) | 0x80;  // Variant 10

    return uuid;
}

// Helper: Parse "host:port" string
std::pair<std::string, uint16_t> parse_target_string(const std::string& target)
{
    auto colon = target.rfind(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid target format (expected host:port): " + target);
    }

    std::string host = target.substr(0, colon);
    std::string port_str = target.substr(colon + 1);

    try {
        uint16_t port = static_cast<uint16_t>(std::stoul(port_str));
        return {host, port};
    } catch (...) {
        throw std::runtime_error("Invalid port number: " + port_str);
    }
}

// UpsairsClient implementation

UpsairsClient::UpsairsClient(const std::vector<std::string>& targets,
                             const Uuid& region_uuid,
                             uint32_t block_size,
                             uint64_t total_blocks,
                             bool read_only,
                             bool encrypted)
    : targets_(targets)
    , region_uuid_(region_uuid)
    , upstairs_id_(generate_uuid())
    , session_id_(generate_uuid())
    , block_size_(block_size)
    , total_blocks_(total_blocks)
    , read_only_(read_only)
    , encrypted_(encrypted)
{
    if (targets.size() != 3) {
        throw std::runtime_error("Crucible requires exactly 3 downstairs targets");
    }

    if (block_size == 0 || (block_size & (block_size - 1)) != 0) {
        throw std::runtime_error("Block size must be power of 2");
    }
}

UpsairsClient::~UpsairsClient()
{
    disconnect();
}

void UpsairsClient::connect()
{
    if (running_) {
        return;  // Already connected
    }

    // Parse targets and establish connections
    for (size_t i = 0; i < 3; i++) {
        try {
            auto target_pair = parse_target_string(targets_[i]);
            std::string host = target_pair.first;
            uint16_t port = target_pair.second;
            connections_[i].reset(new Connection(host, port));
            connected_count_++;

            kprintf("[Crucible] connected to downstairs %zu (%s:%u)\n",
                    i, host.c_str(), port);
        } catch (const std::exception& e) {
            kprintf("[Crucible] connect to downstairs %zu (%s) failed: %s\n",
                    i, targets_[i].c_str(), e.what());
        }
    }

    if (connected_count_ < 2) {
        disconnect();
        throw std::runtime_error("Failed to connect to at least 2 downstairs servers");
    }

    // Perform handshake with all connected downstairs
    for (size_t i = 0; i < 3; i++) {
        if (connections_[i] && connections_[i]->is_connected()) {
            try {
                handshake(i);
                query_region_info(i);
            } catch (const std::exception& e) {
                kprintf("[Crucible] Handshake/query failed for downstairs %zu: %s\n", i, e.what());
                connections_[i]->close();
                connections_[i].reset();
                connected_count_--;
            }
        }
    }

    if (connected_count_ < 2) {
        disconnect();
        throw std::runtime_error("Failed to complete handshake with at least 2 downstairs");
    }

    // Start the per-downstairs sender threads, then the I/O (response) thread.
    running_ = true;
    start_senders();
    io_thread_ = sched::thread::make([this] { this->io_loop(); });
    io_thread_->start();

    kprintf("[Crucible] upstairs ready (%d/3 downstairs)\n",
            connected_count_.load());
}

void UpsairsClient::disconnect()
{
    if (running_) {
        running_ = false;

        // io_loop polls running_ every 100 ms in select(), so it exits on its
        // own; join it before tearing down the senders and sockets.
        if (io_thread_) {
            io_thread_->join();
            delete io_thread_;
            io_thread_ = nullptr;
        }

        // Stop the sender threads (wakes idle ones, closes sockets to unblock
        // any wedged in a blocking send(), then joins).
        stop_senders();
    }

    // Reset connections
    for (auto& conn : connections_) {
        if (conn) {
            conn->close();
            conn.reset();
        }
    }

    connected_count_ = 0;

    // Cancel pending requests
    request_mgr_.cancel_all();
}

void UpsairsClient::start_senders()
{
    for (int i = 0; i < 3; i++) {
        auto& s = senders_[i];
        s.queue.clear();
        s.running = true;
        s.thread = sched::thread::make([this, i] { this->sender_loop(i); });
        s.thread->start();
    }
}

void UpsairsClient::stop_senders()
{
    // Signal each sender to stop and wake any blocked on an empty queue.
    for (int i = 0; i < 3; i++) {
        auto& s = senders_[i];
        WITH_LOCK(s.mtx) {
            s.running = false;
            s.cv.wake_all();
        }
    }

    // A sender wedged in a blocking send() under TCP backpressure will not
    // observe running_ until its socket operation returns.  Close the sockets
    // so that send() fails immediately and the loop exits, instead of blocking
    // join() forever.
    for (auto& conn : connections_) {
        if (conn) {
            conn->close();
        }
    }

    for (int i = 0; i < 3; i++) {
        auto& s = senders_[i];
        if (s.thread) {
            s.thread->join();
            delete s.thread;
            s.thread = nullptr;
        }
        s.queue.clear();
    }
}

bool UpsairsClient::enqueue_frame(int downstairs_idx,
                                  std::vector<uint8_t> header,
                                  std::vector<uint8_t> data)
{
    auto& conn = connections_[downstairs_idx];
    if (!conn || !conn->is_connected()) {
        return false;
    }
    auto& s = senders_[downstairs_idx];
    WITH_LOCK(s.mtx) {
        if (!s.running) {
            return false;
        }
        s.queue.push_back(SendFrame{std::move(header), std::move(data)});
        s.cv.wake_one();
    }
    return true;
}

void UpsairsClient::sender_loop(int downstairs_idx)
{
    auto& s = senders_[downstairs_idx];
    while (true) {
        SendFrame frame;
        WITH_LOCK(s.mtx) {
            while (s.running && s.queue.empty()) {
                s.cv.wait(&s.mtx);
            }
            if (!s.running && s.queue.empty()) {
                return;
            }
            frame = std::move(s.queue.front());
            s.queue.pop_front();
        }

        auto& conn = connections_[downstairs_idx];
        if (!conn || !conn->is_connected()) {
            // Socket went away (reconnect in progress); drop the frame.  The
            // owning request's quorum is satisfied by the other downstairs, or
            // fails via fail_downstairs() / the wait_for_quorum backstop.
            continue;
        }

        try {
            // Header and (optional) data go back to back under the connection
            // send mutex so they reach the wire as one contiguous Crucible
            // frame.  This sender is now the sole writer on the data path for
            // this downstairs, so no other thread can interleave bytes.
            if (frame.data.empty()) {
                conn->send_exact(frame.header.data(), frame.header.size());
            } else {
                conn->send_exact_with_data(
                    frame.header.data(), frame.header.size(),
                    frame.data.data(), frame.data.size());
            }
        } catch (const std::exception& e) {
            // Send failed: close the socket so io_loop's reconnect path takes
            // over.  In-flight requests to this downstairs are failed by
            // fail_downstairs() when io_loop notices the closed connection.
            kprintf("[Crucible] sender %d: send failed: %s\n",
                    downstairs_idx, e.what());
            conn->close();
        }
    }
}

bool UpsairsClient::is_connected() const
{
    return connected_count_ >= 2;
}

int UpsairsClient::read_sync(uint64_t offset, uint32_t length, void* buffer)
{
    if (!is_connected()) {
        return EIO;
    }

    // Validate parameters
    if (offset + length > total_size()) {
        return EINVAL;
    }

    if (length % block_size_ != 0 || offset % block_size_ != 0) {
        return EINVAL;
    }

    // Calculate block range
    uint64_t start_block = offset / block_size_;
    uint64_t block_count = length / block_size_;
    uint8_t* data_ptr = static_cast<uint8_t*>(buffer);

    // Allocate job ID
    uint64_t job_id = job_allocator_.allocate();

    // Create pending request
    auto req = request_mgr_.create_request(job_id);

    // Build ReadRequest message
    ReadRequest read_msg;
    read_msg.upstairs_id = upstairs_id_;
    read_msg.session_id = session_id_;
    read_msg.job_id = job_id;
    read_msg.dependencies = {};  // No dependencies for now
    read_msg.start_block = start_block;
    read_msg.count = block_count;

    // Encode message
    auto frame = encode_message(read_msg);

    /*
     * Enqueue to all connected downstairs.  Reads need only 2-of-3
     * responses (set in PendingRequest's required_quorum); we still
     * send to all three so the third can serve as a tiebreaker on
     * checksum mismatch.  Enqueue is non-blocking: a backpressured
     * downstairs stalls only its own sender thread, never this read's
     * dispatch to the other two.
     */
    int sent_count = 0;
    for (int i = 0; i < 3; i++) {
        if (enqueue_frame(i, frame)) {
            sent_count++;
        }
    }

    if (sent_count < 2) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] read job_id=%lu: only %d of 3 downstairs reachable\n",
                job_id, sent_count);
        return EIO;
    }

    if (!req->wait_for_quorum()) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] read job_id=%lu: quorum not reached\n", job_id);
        return EIO;
    }

    // Find first successful response with data
    int source_idx = -1;
    for (int i = 0; i < 3; i++) {
        if (req->downstairs_responded[i] && !req->read_data[i].empty()) {
            source_idx = i;
            break;
        }
    }

    if (source_idx < 0) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] Read job_id=%lu: no valid data received\n", job_id);
        return EIO;
    }

    // Verify hash and copy data
    const auto& data = req->read_data[source_idx];
    const auto& contexts = req->read_contexts[source_idx];

    if (data.size() != length || contexts.size() != block_count) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] Read job_id=%lu: data size mismatch\n", job_id);
        return EIO;
    }

    /*
     * Per-block hash verification.  If a downstairs returned data with
     * an integrity hash mismatch, we have either a bug or storage
     * corruption -- log it and fail the read.  An Empty (no-context)
     * block was never written so the data is whatever was in the extent
     * file (zeros for a fresh region); we accept it without hashing.
     */
    for (uint64_t i = 0; i < block_count; i++) {
        const auto& ctx = contexts[i];
        if (ctx.type == ReadBlockType::Unencrypted) {
            uint64_t computed_hash = xxhash64_block(
                data.data() + i * block_size_, block_size_);
            if (computed_hash != ctx.hash) {
                request_mgr_.remove_request(job_id);
                kprintf("[Crucible] Read job_id=%lu: hash mismatch at block %lu "
                        "(start_block=%lu, count=%lu, expected=0x%016lx, got=0x%016lx)\n",
                        job_id, i, start_block, block_count,
                        ctx.hash, computed_hash);
                return EIO;
            }
        }
    }

    std::memcpy(data_ptr, data.data(), length);
    request_mgr_.remove_request(job_id);
    return 0;
}

int UpsairsClient::write_sync(uint64_t offset, uint32_t length, const void* buffer)
{
    if (!is_connected()) {
        return EIO;
    }

    if (read_only_) {
        return EROFS;
    }

    // Validate parameters
    if (offset + length > total_size()) {
        return EINVAL;
    }

    if (length % block_size_ != 0 || offset % block_size_ != 0) {
        return EINVAL;
    }

    // Calculate block range
    uint64_t start_block = offset / block_size_;
    uint64_t block_count = length / block_size_;
    const uint8_t* data_ptr = static_cast<const uint8_t*>(buffer);

    // Allocate job id, record it as an outstanding write, and depend on the
    // most recent flush so this write is ordered after it on every replica.
    uint64_t job_id;
    auto dependencies = begin_write(job_id);

    // Create pending request
    auto req = request_mgr_.create_request(job_id);

    // Build block contexts with hashes
    std::vector<BlockContext> contexts;
    contexts.reserve(block_count);

    for (uint64_t i = 0; i < block_count; i++) {
        BlockContext ctx;
        ctx.hash = xxhash64_block(data_ptr + i * block_size_, block_size_);
        ctx.encryption_ctx = nullopt;  // No encryption for now
        contexts.push_back(ctx);
    }

    /*
     * Build the Write message header.  The frame is encoded as:
     *   [u32 LE total_len][header bytes][u64 LE data_len=length][data]
     * The header through the u64 data length prefix is produced by
     * encode_message_with_data_header(); we then send the actual block
     * data immediately afterwards on the same socket.
     */
    Write write_msg;
    write_msg.upstairs_id = upstairs_id_;
    write_msg.session_id = session_id_;
    write_msg.job_id = job_id;
    write_msg.dependencies = std::move(dependencies);
    write_msg.start_block = start_block;
    write_msg.contexts = std::move(contexts);

    auto header_frame = encode_message_with_data_header(write_msg, length);

    /*
     * Enqueue the header+data pair to each connected downstairs sender.
     * The sender thread writes the two halves back to back under the
     * connection send mutex, so they reach the wire as one atomic frame
     * (ZFS's TXG sync issues many concurrent Writes; interleaved bytes on
     * the socket trigger "bytes remaining on stream" / disconnect).  The
     * enqueue itself is non-blocking, so a downstairs that is backpressuring
     * TCP stalls only its own sender -- this write still dispatches to the
     * other two and their 2-of-3 quorum completes it.  This is the fix for
     * the 256-MiB flush-contention hang.
     */
    int sent_count = 0;
    for (int i = 0; i < 3; i++) {
        std::vector<uint8_t> data(data_ptr, data_ptr + length);
        if (enqueue_frame(i, header_frame, std::move(data))) {
            sent_count++;
        }
    }

    if (sent_count < 2) {
        abort_write(job_id);
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] write job_id=%lu: only %d of 3 downstairs reachable\n",
                job_id, sent_count);
        return EIO;
    }

    if (!req->wait_for_quorum()) {
        abort_write(job_id);
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] write job_id=%lu: quorum not reached\n", job_id);
        return EIO;
    }

    request_mgr_.remove_request(job_id);
    return 0;
}

int UpsairsClient::flush_sync()
{
    if (!is_connected()) {
        return EIO;
    }

    if (read_only_) {
        return 0;  // No-op for read-only
    }

    // Allocate job id and snapshot every write submitted since the previous
    // flush as this flush's dependencies -- the txg-closing barrier.
    uint64_t job_id;
    auto dependencies = begin_flush(job_id);

    // Increment flush number
    flush_number_++;

    // Create pending request
    auto req = request_mgr_.create_request(job_id);

    // Build Flush message
    Flush flush_msg;
    flush_msg.upstairs_id = upstairs_id_;
    flush_msg.session_id = session_id_;
    flush_msg.job_id = job_id;
    flush_msg.dependencies = std::move(dependencies);
    flush_msg.flush_number = flush_number_;
    flush_msg.gen_number = generation_;
    /*
     * No snapshot, no per-extent flush limit: both fields are encoded as
     * None (single 0 byte each).  Setting extent_limit to extent_count
     * was a leftover that the upstream protocol does not expect.
     */
    flush_msg.snapshot_name = nullopt;
    flush_msg.extent_limit = nullopt;

    auto frame = encode_message(flush_msg);

    /*
     * Enqueue to all connected downstairs (non-blocking).  A flush makes the
     * downstairs fsync every dirty extent, which is the slow operation that
     * backpressures TCP; routing it through the per-downstairs sender queue
     * is exactly what keeps one slow replica from stalling the flush to the
     * other two, so the 2-of-3 quorum still completes.
     */
    int sent_count = 0;
    for (int i = 0; i < 3; i++) {
        if (enqueue_frame(i, frame)) {
            sent_count++;
        }
    }

    if (sent_count < 2) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] flush job_id=%lu: only %d of 3 downstairs reachable\n",
                job_id, sent_count);
        return EIO;
    }

    if (!req->wait_for_quorum()) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] flush job_id=%lu: quorum not reached\n", job_id);
        return EIO;
    }

    // Barrier was advanced at allocation in begin_flush(); nothing to commit.
    request_mgr_.remove_request(job_id);
    return 0;
}

int UpsairsClient::create_snapshot(uint64_t snapshot_id)
{
    if (!is_connected()) {
        return EIO;
    }

    if (read_only_) {
        return EROFS;  // Cannot create snapshots in read-only mode
    }

    // Snapshots require 3/3 quorum (not 2/3)
    if (connected_count_ < 3) {
        kprintf("[Crucible] Snapshot requires 3/3 downstairs (only %d connected)\n",
                connected_count_.load());
        return EIO;
    }

    // Allocate job id and snapshot outstanding writes as dependencies: a
    // snapshot is a Flush, so it must also wait for every prior write.
    uint64_t job_id;
    auto dependencies = begin_flush(job_id);

    // Increment flush number
    flush_number_++;

    // Create pending request (requires 3/3 acknowledgments)
    auto req = request_mgr_.create_request(job_id, 3);

    /*
     * Build a snapshot Flush.  The snapshot ID is stringified into
     * SnapshotDetails.snapshot_name, which is what upstream Crucible
     * accepts.  No per-extent limit.
     */
    char snapname[32];
    snprintf(snapname, sizeof(snapname), "%lu", (unsigned long)snapshot_id);

    Flush flush_msg;
    flush_msg.upstairs_id = upstairs_id_;
    flush_msg.session_id = session_id_;
    flush_msg.job_id = job_id;
    flush_msg.dependencies = std::move(dependencies);
    flush_msg.flush_number = flush_number_;
    flush_msg.gen_number = generation_;
    flush_msg.snapshot_name = std::string(snapname);
    flush_msg.extent_limit = nullopt;

    auto frame = encode_message(flush_msg);

    /*
     * Snapshots require 3/3 acknowledgement (not 2/3): a downstairs
     * that doesn't see the snapshot Flush would silently miss the
     * point-in-time the user asked us to capture.  Enqueue to all three.
     */
    int sent_count = 0;
    for (int i = 0; i < 3; i++) {
        if (enqueue_frame(i, frame)) {
            sent_count++;
        }
    }

    if (sent_count < 3) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] snapshot %lu: only %d of 3 downstairs reachable\n",
                snapshot_id, sent_count);
        return EIO;
    }

    if (!req->wait_for_quorum()) {
        request_mgr_.remove_request(job_id);
        kprintf("[Crucible] snapshot %lu: 3/3 quorum not reached\n", snapshot_id);
        return EIO;
    }

    // Snapshot flush reached 3/3; the barrier was advanced at allocation in
    // begin_flush(), so there is nothing more to commit here.
    request_mgr_.remove_request(job_id);
    return 0;
}

int UpsairsClient::discard_sync(uint64_t offset, uint64_t length)
{
    /*
     * The upstream Crucible protocol V13 has no Discard / DiscardAck
     * messages.  TRIM/UNMAP is silently unsupported here; report
     * ENOTSUP so the bio layer can complete the I/O and let the
     * filesystem fall back to overwrite-with-zero or skip the trim.
     */
    (void) offset;
    (void) length;
    return ENOTSUP;
}

std::pair<std::string, uint16_t> UpsairsClient::parse_target(const std::string& target)
{
    return parse_target_string(target);
}

std::vector<uint64_t> UpsairsClient::begin_write(uint64_t& job_id)
{
    WITH_LOCK(dep_mtx_) {
        job_id = job_allocator_.allocate();
        // A write issued after a flush must be applied after it.
        std::vector<uint64_t> deps;
        if (last_flush_id_ != 0) {
            deps.push_back(last_flush_id_);
        }
        // Record the write immediately, in id-allocation order, so any Flush
        // allocated after this point lists it as a dependency.  This is the
        // load-bearing invariant: the downstairs CompletedJobs set is a
        // watermark (downstairs/src/complete_jobs.rs) and a FlushAck calls
        // reset(flush_id), forgetting every lower job id.  If a Flush did not
        // depend on this still-outstanding write, the downstairs could apply
        // the Flush first, reset the watermark past this write's own flush
        // dependency, and then block this write forever -- its dep id now sits
        // below the watermark and is_complete() returns false for good.
        // Listing every prior write as a Flush dep forces the downstairs to
        // apply the write before the Flush, so the reset never strands it.
        // Recording at allocation (not after quorum) is what makes the
        // out-of-order sends from the async block dispatcher safe: the wire
        // order may differ from allocation order, but the dep lists encode the
        // true allocation order, so the downstairs reconstructs it.
        pending_writes_.push_back(job_id);
        return deps;
    }
}

void UpsairsClient::abort_write(uint64_t job_id)
{
    WITH_LOCK(dep_mtx_) {
        // A write that failed to reach quorum is fatal to the ZFS txg (the
        // caller returns EIO), but drop it from the pending set anyway so a
        // later Flush -- if one is still built before the pool suspends -- does
        // not list a write id that no downstairs durably accepted.  If a Flush
        // already snapshotted and cleared it, this is a harmless no-op.
        auto it = std::find(pending_writes_.begin(), pending_writes_.end(), job_id);
        if (it != pending_writes_.end()) {
            pending_writes_.erase(it);
        }
    }
}

std::vector<uint64_t> UpsairsClient::begin_flush(uint64_t& job_id)
{
    WITH_LOCK(dep_mtx_) {
        job_id = job_allocator_.allocate();
        // This flush depends on every write since the previous flush, plus
        // that previous flush, so the chain of txg barriers stays ordered.
        std::vector<uint64_t> deps = pending_writes_;
        if (last_flush_id_ != 0) {
            deps.push_back(last_flush_id_);
        }
        // Advance the barrier at allocation, not after quorum.  The deps list
        // above is the complete set of writes this Flush orders; once the id is
        // allocated, the next write must depend on THIS flush and the writes it
        // covers are behind the barrier.  Deferring the advance to a post-quorum
        // commit reopened the allocation-order gap the downstairs watermark
        // requires (a write recorded only post-quorum could be omitted from an
        // already-allocated flush's deps -> reset strands it).  A flush that
        // fails quorum returns EIO, which is fatal to the txg, so there is no
        // surviving pool state that needs the barrier rolled back.
        pending_writes_.clear();
        last_flush_id_ = job_id;
        return deps;
    }
}

void UpsairsClient::io_loop()
{
    /*
     * Crucible downstairs drop a connection after 45 s of inactivity
     * (downstairs VerboseTimeout = 3 × 15 s).  ZFS workloads have long idle
     * gaps — notably the label/uberblock probe phase of zpool_create — during
     * which the OSv driver would otherwise send nothing, the downstairs would
     * time out, and the resulting reconnect churn would stall (or, before the
     * tcp_input fast-path guard, panic) the kernel.  Mirror the real upstairs:
     * send a Ruok ping to every connected downstairs whenever the link has
     * been idle, well inside the 45 s window.  The Imok reply is handled in
     * process_responses(); we do not block waiting for it here.
     */
    constexpr auto keepalive_interval = std::chrono::seconds(10);
    auto next_ping = std::chrono::steady_clock::now() + keepalive_interval;

    while (running_) {
        if (std::chrono::steady_clock::now() >= next_ping) {
            send_keepalive();
            next_ping = std::chrono::steady_clock::now() + keepalive_interval;
        }

        // Use select() to wait for readable connections
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;

        for (size_t i = 0; i < 3; i++) {
            if (connections_[i] && connections_[i]->is_connected()) {
                int fd = connections_[i]->fd();
                FD_SET(fd, &readfds);
                if (fd > max_fd) {
                    max_fd = fd;
                }
            }
        }

        if (max_fd < 0) {
            // No connections, sleep
            sched::thread::sleep(std::chrono::milliseconds(100));
            continue;
        }

        // Wait with timeout
        struct timeval tv = {0, 100000};  // 100ms
        int ret = select(max_fd + 1, &readfds, nullptr, nullptr, &tv);

        if (ret < 0) {
            if (errno == EINTR) {
                // Interrupted, retry
                continue;
            }
            kprintf("[Crucible] select() error: %d\n", errno);
            break;
        }

        if (ret > 0) {
            // Process responses from readable connections
            for (size_t i = 0; i < 3; i++) {
                if (connections_[i] && connections_[i]->is_connected()) {
                    int fd = connections_[i]->fd();
                    if (FD_ISSET(fd, &readfds)) {
                        try {
                            process_responses(i);
                        } catch (const std::exception& e) {
                            kprintf("[Crucible] Downstairs %zu connection lost: %s\n",
                                    i, e.what());
                            connections_[i]->close();
                            connected_count_--;

                            // Fail this downstairs against every in-flight
                            // request so an op that just dropped below 2/3
                            // quorum returns now instead of waiting out the
                            // full wait_for_quorum backstop.  Ops that still
                            // have 2 live downstairs keep waiting on them.
                            request_mgr_.fail_downstairs(i);

                            // Attempt reconnect with exponential backoff.
                            // I/O thread stays alive so other downstairs keep working.
                            sched::thread::make([this, i] {
                                int delay_ms = 100;
                                while (running_) {
                                    sched::thread::sleep(
                                        std::chrono::milliseconds(delay_ms));
                                    try {
                                        connections_[i]->reconnect();
                                        handshake(i);
                                        query_region_info(i);
                                        connected_count_++;
                                        kprintf("[Crucible] Downstairs %zu reconnected\n", i);
                                        return;
                                    } catch (const std::exception& re) {
                                        kprintf("[Crucible] Downstairs %zu reconnect failed"
                                                " (retry in %d ms): %s\n",
                                                i, delay_ms, re.what());
                                        delay_ms = std::min(delay_ms * 2, 30000);
                                    }
                                }
                            }, sched::thread::attr().detached())->start();
                        }
                    }
                }
            }
        }

    }
}

void UpsairsClient::send_keepalive()
{
    /*
     * Encode a single Ruok frame and enqueue it to every connected
     * downstairs sender.  Enqueue is the right primitive here: if a
     * downstairs's queue already has frames the link is actively sending
     * (not idle), so the ping harmlessly trails real traffic; if the link
     * is idle the queue is empty and the ping goes out immediately, well
     * inside the downstairs 45 s inactivity timeout.  io_loop never blocks
     * on the send path -- that work belongs to the sender threads now.
     */
    Ruok ping;
    auto frame = encode_message(ping);

    for (int i = 0; i < 3; i++) {
        enqueue_frame(i, frame);
    }
}

void UpsairsClient::process_responses(int downstairs_idx)
{
    // Receive frame
    auto frame = receive_frame(downstairs_idx);

    // Decode message type
    bincode::Decoder dec(frame);
    MessageType type = decode_message_type(dec);

    /*
     * Handle responses from a downstairs.
     *
     * Late acks are expected under load: write_sync / read_sync / flush_sync
     * complete and remove the request as soon as 2-of-3 quorum is reached.
     * The third downstairs's ack/response then arrives after the request
     * is gone — that is normal, not a bug, and we silently drop it.  The
     * old kprintf for these "unknown job" messages produced enough console
     * output under ZFS workloads to crash the kernel with
     * "exception nested too deeply" inside the printf path.
     */
    switch (type) {
        case MessageType::WriteAck: {
            auto ack = WriteAck::decode(dec);
            auto req = request_mgr_.find_request(ack.job_id);
            if (req) {
                req->mark_response(downstairs_idx, ack.result.is_ok,
                                  ack.result.is_ok ? CrucibleError::IoError : ack.result.error);
            }
            break;
        }

        case MessageType::ReadResponse: {
            auto resp = ReadResponse::decode_header(dec);
            auto req = request_mgr_.find_request(resp.job_id);
            if (resp.blocks.is_ok) {
                /*
                 * The bulk data is part of the same frame, encoded inline
                 * as bincode `bytes` (u64 length + raw bytes).  Read both
                 * from the in-memory frame buffer; do not pull more bytes
                 * from the socket -- the socket cursor is already at the
                 * start of the next frame.
                 */
                uint64_t data_len = dec.decode_byte_slice_length();
                std::vector<uint8_t> data = dec.decode_bytes(data_len);
                if (req) {
                    req->read_data[downstairs_idx] = std::move(data);
                    req->read_contexts[downstairs_idx] =
                        std::move(resp.blocks.value);
                    req->mark_response(downstairs_idx, true);
                }
                /* else: late 3rd-of-3 read response, silently drop. */
            } else {
                if (req) {
                    req->mark_response(downstairs_idx, false,
                                       resp.blocks.error);
                }
            }
            break;
        }

        case MessageType::FlushAck: {
            auto ack = FlushAck::decode(dec);
            auto req = request_mgr_.find_request(ack.job_id);
            if (req) {
                req->mark_response(downstairs_idx, ack.result.is_ok,
                                  ack.result.is_ok ? CrucibleError::IoError : ack.result.error);
            }
            break;
        }

        case MessageType::Imok: {
            /* Health check response. Quiet success: no log. */
            (void) downstairs_idx;
            break;
        }

        default:
            kprintf("[Crucible] Unexpected message type from downstairs %d: %u\n",
                  downstairs_idx, static_cast<uint32_t>(type));
            break;
    }
}

void UpsairsClient::handshake(int downstairs_idx)
{
    auto& conn = connections_[downstairs_idx];
    if (!conn || !conn->is_connected()) {
        throw ConnectionError("Downstairs not connected");
    }

    // Send HereIAm
    HereIAm here_msg;
    here_msg.version = static_cast<uint32_t>(ProtocolVersion::V13);
    here_msg.upstairs_id = upstairs_id_;
    here_msg.session_id = session_id_;
    here_msg.gen = generation_;
    here_msg.read_only = read_only_;
    here_msg.encrypted = encrypted_;
    /* No alternate versions advertised — upstairs only speaks V13. */
    here_msg.alternate_versions.clear();

    auto frame = encode_message(here_msg);
    conn->send_exact(frame.data(), frame.size());

    // Receive response
    auto response_frame = receive_frame(downstairs_idx);
    bincode::Decoder dec(response_frame);

    MessageType type = decode_message_type(dec);

    if (type == MessageType::YesItsMe) {
        auto yes_msg = YesItsMe::decode(dec);

        if (yes_msg.version != static_cast<uint32_t>(ProtocolVersion::V13)) {
            throw std::runtime_error("Version mismatch in YesItsMe: got " +
                                      std::to_string(yes_msg.version));
        }

        /*
         * Upstream Crucible protocol step 2: send PromoteToActive and wait
         * for YouAreNowActive (or YouAreNoLongerActive if another upstairs
         * has stolen the slot).  Only after this exchange may we issue
         * RegionInfoPlease and start I/O.
         */
        PromoteToActive promote;
        promote.upstairs_id = upstairs_id_;
        promote.session_id = session_id_;
        promote.generation = generation_;
        auto promote_frame = encode_message(promote);
        conn->send_exact(promote_frame.data(), promote_frame.size());

        auto resp_frame = receive_frame(downstairs_idx);
        bincode::Decoder resp_dec(resp_frame);
        MessageType resp_type = decode_message_type(resp_dec);
        if (resp_type == MessageType::YouAreNowActive) {
            (void) YouAreNowActive::decode(resp_dec);
            kprintf("[Crucible] downstairs %d active (repair port %u)\n",
                    downstairs_idx, yes_msg.repair_addr.port);
        } else if (resp_type == MessageType::YouAreNoLongerActive) {
            throw std::runtime_error("Promote rejected: another session is active");
        } else {
            throw std::runtime_error("Unexpected promote response: " +
                                     std::to_string(static_cast<uint32_t>(resp_type)));
        }

    } else if (type == MessageType::VersionMismatch) {
        auto mismatch = VersionMismatch::decode(dec);
        throw std::runtime_error("Version mismatch: offered " +
                                std::to_string(mismatch.offered));

    } else if (type == MessageType::ReadOnlyMismatch) {
        throw std::runtime_error("Read-only mode mismatch");

    } else if (type == MessageType::EncryptedMismatch) {
        throw std::runtime_error("Encryption mode mismatch");

    } else if (type == MessageType::UuidMismatch) {
        throw std::runtime_error("Region UUID mismatch");

    } else {
        throw std::runtime_error("Unexpected handshake response: " +
                                std::to_string(static_cast<uint32_t>(type)));
    }
}

void UpsairsClient::query_region_info(int downstairs_idx)
{
    auto& conn = connections_[downstairs_idx];
    if (!conn || !conn->is_connected()) {
        throw ConnectionError("Downstairs not connected");
    }

    RegionInfoPlease req_msg;
    auto frame = encode_message(req_msg);
    conn->send_exact(frame.data(), frame.size());

    auto response_frame = receive_frame(downstairs_idx);
    bincode::Decoder dec(response_frame);

    MessageType type = decode_message_type(dec);
    if (type != MessageType::RegionInfo) {
        throw std::runtime_error("Expected RegionInfo, got " +
                                std::to_string(static_cast<uint32_t>(type)));
    }

    auto info_msg = RegionInfo::decode(dec);
    region_def_ = info_msg.region_def;

    // Validate region definition
    if (region_def_.block_size != block_size_) {
        throw std::runtime_error("Block size mismatch: expected " +
                                std::to_string(block_size_) + ", got " +
                                std::to_string(region_def_.block_size));
    }

    /*
     * Set total_blocks_ from the region definition the first time we
     * see it; subsequent downstairs are validated against the same
     * geometry by the block_size check above.
     */
    if (total_blocks_ == 0) {
        total_blocks_ = region_def_.extent_size * region_def_.extent_count;
        kprintf("[Crucible] region: %lu blocks of %lu bytes (%u extents x %lu)\n",
                total_blocks_, region_def_.block_size,
                region_def_.extent_count, region_def_.extent_size);
    }

    /*
     * Final negotiation step: send ExtentVersionsPlease so the downstairs
     * transitions out of "waiting for extent versions" and starts treating
     * incoming Write/Flush/ReadRequest messages as I/O instead of
     * "ignored message during negotiation".  We don't act on the per-extent
     * gen/flush/dirty data — the OSv driver doesn't implement live repair —
     * but we have to consume the bytes off the socket so the next reply
     * isn't misframed.
     */
    ExtentVersionsPlease ev_req;
    auto ev_frame = encode_message(ev_req);
    conn->send_exact(ev_frame.data(), ev_frame.size());

    auto ev_resp_frame = receive_frame(downstairs_idx);
    bincode::Decoder ev_dec(ev_resp_frame);
    MessageType ev_type = decode_message_type(ev_dec);
    if (ev_type != MessageType::ExtentVersions) {
        throw std::runtime_error("Expected ExtentVersions, got " +
                                  std::to_string(static_cast<uint32_t>(ev_type)));
    }
    (void) ExtentVersions::decode(ev_dec);
}

std::vector<uint8_t> UpsairsClient::receive_frame(int downstairs_idx)
{
    auto& conn = connections_[downstairs_idx];
    if (!conn || !conn->is_connected()) {
        throw ConnectionError("Downstairs not connected");
    }

    /*
     * The Rust upstream Crucible encoder writes the 4-byte u32 LE prefix
     * as TOTAL frame length (prefix + payload), so subtract 4 to get the
     * payload length we still need to read.
     */
    uint32_t total_length;
    conn->recv_exact(&total_length, 4);

    if (total_length < 4 || total_length > 100 * 1024 * 1024) {
        throw std::runtime_error("Frame size out of range");
    }
    uint32_t payload_length = total_length - 4;

    std::vector<uint8_t> data(payload_length);
    conn->recv_exact(data.data(), payload_length);

    return data;
}

} // namespace crucible
