/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "crucible-request.hh"
#include <osv/debug.h>

namespace crucible {

void PendingRequest::mark_response(int downstairs_idx, bool success,
                                   CrucibleError error)
{
    WITH_LOCK(mtx) {
        if (completed || downstairs_responded[downstairs_idx]) {
            // Duplicate response, ignore
            return;
        }

        downstairs_responded[downstairs_idx] = true;
        downstairs_succeeded[downstairs_idx] = success;
        responses_received.fetch_add(1, std::memory_order_relaxed);

        if (success) {
            success_count.fetch_add(1, std::memory_order_relaxed);
        } else {
            error_count.fetch_add(1, std::memory_order_relaxed);
            downstairs_errors[downstairs_idx] = error;
        }

        // Check if quorum reached
        if (has_quorum()) {
            completed = true;

            if (success_count >= required_quorum) {
                result = Result<void>::ok();
            } else {
                result = Result<void>::err(error);
            }

            cv.wake_all();
        }
    }
}

bool PendingRequest::wait_for_quorum(unsigned timeout_ms)
{
    WITH_LOCK(mtx) {
        auto deadline = start_time + std::chrono::milliseconds(timeout_ms);

        while (!completed) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                // Timeout
                completed = true;
                result = Result<void>::err(CrucibleError::Timeout);
                return false;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            cv.wait(&mtx, remaining);
        }

        return result.is_ok;
    }
}

// RequestManager implementation

RequestManager::RequestManager()
{
}

RequestManager::~RequestManager()
{
    cancel_all();
}

std::shared_ptr<PendingRequest> RequestManager::create_request(uint64_t job_id, MessageType type,
                                                               uint32_t read_length, int required_quorum)
{
    auto req = std::make_shared<PendingRequest>(job_id, type, read_length, required_quorum);

    WITH_LOCK(mtx_) {
        requests_[job_id] = req;
    }

    return req;
}

std::shared_ptr<PendingRequest> RequestManager::find_request(uint64_t job_id)
{
    WITH_LOCK(mtx_) {
        auto it = requests_.find(job_id);
        if (it != requests_.end()) {
            return it->second;
        }
    }

    return nullptr;
}

void RequestManager::remove_request(uint64_t job_id)
{
    WITH_LOCK(mtx_) {
        requests_.erase(job_id);
    }
}

size_t RequestManager::pending_count() const
{
    WITH_LOCK(mtx_) {
        return requests_.size();
    }
}

void RequestManager::cancel_all()
{
    std::map<uint64_t, std::shared_ptr<PendingRequest>> pending;
    WITH_LOCK(mtx_) {
        pending.swap(requests_);
    }
    for (auto& pair : pending) {
        auto& req = pair.second;
        WITH_LOCK(req->mtx) {
            if (!req->completed) {
                req->completed = true;
                req->result = Result<void>::err(CrucibleError::ConnectionError);
                req->cv.wake_all();
            }
        }
    }
}

void RequestManager::fail_downstairs(int downstairs_idx)
{
    // Map -> request is the sole nested lock order. Avoid allocating while
    // quarantining: this path must also work after std::bad_alloc.
    WITH_LOCK(mtx_) {
        for (auto& pair : requests_) {
            pair.second->mark_response(downstairs_idx, false, CrucibleError::ConnectionError);
        }
    }
}

} // namespace crucible
