// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
using namespace virtio;

int main(int argc, char** argv)
{
    CHECK(argc == 3);
    alarm(5);
    const std::string mode = argv[1];
    const bool indirect = std::string(argv[2]) == "indirect";
    virtio_driver driver;
    vring q(&driver, 8, 0);
    q.set_use_indirect(indirect);
    auto* desc = reinterpret_cast<vring_desc*>(q.get_desc_addr());
    auto* avail = reinterpret_cast<vring_avail*>(q.get_avail_addr());
    auto* used = reinterpret_cast<vring_used*>(q.get_used_addr());
    int cookies[2] = {}, payload = 0;
    u16 posted = 0;
    auto submit = [&](unsigned n, void* cookie) {
        q.init_sg();
        for (unsigned i = 0; i < n; ++i) q.add_out_sg(&payload, sizeof(payload));
        CHECK(q.add_buf(cookie));
        return avail->_ring[u16(avail->_idx.load() - 1) & 7];
    };
    auto complete = [&](u32 id) {
        used->_used_elements[posted & 7] = {id, 123};
        used->_idx.store(++posted, std::memory_order_release);
    };
    auto consume = [&](void* cookie) {
        u32 len = 0;
        CHECK(q.get_buf_elem(&len) == cookie);
        CHECK(len == 123);
        q.get_buf_finalize();
    };

    if (mode == "valid") {
        // Exactly eight direct descriptors (terminal NEXT clear), or nine SGs
        // forcing a real indirect allocation, across both u16 cursor wraps.
        for (unsigned i = 0; i < 70000; ++i) {
            auto head = submit(indirect ? 9 : 8, cookies);
            CHECK(bool(desc[head]._flags & vring_desc::VRING_DESC_F_INDIRECT) == indirect);
            complete(head);
            consume(cookies);
            q.get_buf_gc();
            CHECK(q.effective_avail_ring_count() == 8);
            CHECK(!q.used_ring_can_gc());
            CHECK(memory::allocations - memory::frees == 1);
        }
        u32 len = 456;
        CHECK(q.get_buf_elem(&len) == nullptr);
        CHECK(len == 456);
        return 0;
    }

    auto head = submit(indirect ? 9 : mode == "full-cycle" ? 8 : 3, cookies);
    const auto free_before = memory::frees;
    if (mode == "id" || mode == "id-max") {
        auto second = submit(1, cookies + 1);
        complete(mode == "id" ? 8 : UINT32_MAX);
        complete(second); // Baseline skips bad entry and returns this cookie.
        const auto count = q.effective_avail_ring_count();
        const auto next = q.avail_head();
        u32 len = 456;
        at_fatal = [&](const char* reason) {
            CHECK(std::string(reason) == (mode == "id"
                ? "virtio: queue 0 invalid descriptor index 8 (size 8)\n"
                : "virtio: queue 0 invalid descriptor index 4294967295 (size 8)\n"));
            CHECK(len == 456);
            CHECK(q.effective_avail_ring_count() == count);
            CHECK(!q.used_ring_can_gc());
            CHECK(q.avail_head() == next);
            CHECK(memory::frees == free_before);
        };
        q.get_buf_elem(&len);
        CHECK(false); // Must not complete another request or skip the bad ID.
    }

    complete(head);
    consume(cookies);
    if (mode == "gc-id" || mode == "gc-id-size") {
        // Device changes an already-consumed used entry before producer GC.
        used->_used_elements[0]._id = mode == "gc-id" ? UINT32_MAX : 8;
    } else {
        CHECK(!indirect); // NEXT is guest-owned direct-chain metadata.
        if (mode == "next") desc[head]._next = UINT16_MAX;
        else if (mode == "next-size") desc[head]._next = 8;
        else if (mode == "self-cycle") desc[head]._next = head;
        else if (mode == "full-cycle") {
            desc[7]._flags |= vring_desc::VRING_DESC_F_NEXT;
            desc[7]._next = head;
        }
        else if (mode == "cycle") {
            desc[head + 2]._flags |= vring_desc::VRING_DESC_F_NEXT;
            desc[head + 2]._next = head;
        } else CHECK(false);
    }
    const auto count = q.effective_avail_ring_count();
    const auto next = q.avail_head();
    vring_desc before[8];
    std::memcpy(before, desc, sizeof(before));
    at_fatal = [&](const char* reason) {
        CHECK(std::memcmp(before, desc, sizeof(before)) == 0);
        CHECK(std::string(reason) == (mode == "gc-id"
            ? "virtio: queue 0 invalid descriptor index 4294967295 (size 8)\n"
            : mode == "gc-id-size" || mode == "next-size"
            ? "virtio: queue 0 invalid descriptor index 8 (size 8)\n"
            : mode == "next"
            ? "virtio: queue 0 invalid descriptor index 65535 (size 8)\n"
            : "virtio: queue 0 cyclic descriptor chain\n"));
        CHECK(q.effective_avail_ring_count() == count);
        CHECK(q.used_ring_can_gc());
        CHECK(q.avail_head() == next);
        CHECK(memory::frees == free_before);
    };
    q.get_buf_gc();
    CHECK(false); // No cursor/availability publication or reclamation on error.
}
