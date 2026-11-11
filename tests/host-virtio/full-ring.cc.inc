// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
#include <cerrno>
#define trace_virtio_blk_req_ok(...) ((void)0)
#define trace_virtio_blk_req_unsupp(...) ((void)0)
#define trace_virtio_blk_req_err(...) ((void)0)
#define trace_virtio_blk_wake(...) ((void)0)
#define trace_virtio_blk_make_request_readonly(...) ((void)0)
#define trace_virtio_blk_make_request_seg_max(...) ((void)0)
static const int sector_size = 512;
enum { BIO_READ, BIO_WRITE, BIO_FLUSH, BIO_DISCARD };
struct bio {
    int bio_cmd = BIO_FLUSH;
    size_t bio_bcount = 0;
    off_t bio_offset = 0;
    void* bio_data = nullptr;
    bool done = false;
};
void biodone(bio* b, bool ok) { assert(ok); b->done = true; }
namespace mmu { constexpr size_t page_size = 4096; }
namespace sched {
struct cpu { int id = 0; static cpu* current() { static cpu c; return &c; } };
}
namespace virtio {
class blk {
public:
    struct blk_outhdr { u32 type, ioprio; u64 sector; };
    struct blk_res { u8 status; };
    struct blk_req {
        explicit blk_req(struct bio* b) : bio(b) {}
        blk_outhdr hdr;
        blk_res res;
        struct bio* bio;
        struct { u64 sector; u32 num_sectors, flags; } discard_desc;
    };
    enum blk_request_type { VIRTIO_BLK_T_IN, VIRTIO_BLK_T_OUT,
                           VIRTIO_BLK_T_FLUSH = 4, VIRTIO_BLK_T_DISCARD = 11 };
    enum { VIRTIO_BLK_F_SEG_MAX = 2, VIRTIO_BLK_F_DISCARD = 13 };
    static constexpr int VIRTIO_BLK_S_OK = 0, VIRTIO_BLK_S_UNSUPP = 2;
    int _num_queues = 1;
    std::vector<mutex> _queue_locks{1};
    struct { u32 seg_max = 0, max_discard_sectors = 0; } _config;
    vring* ring;
    vring* get_virt_queue(int) { return ring; }
    bool is_readonly() { return false; }
    bool get_guest_feature_bit(int) { return false; }
    static int drain_queue(vring*);
    bool any_queue_not_empty();
    void req_done();
    int make_request(struct bio*);
};
}
int main() {
    // Both producer and consumer are production methods, as is the ring.
    // One FLUSH consumes both direct descriptors. The second submission
    // waits holding its production mutex before we expose a completion.
    alarm(5);
    virtio::virtio_driver driver;
    virtio::vring ring(&driver, 2, 0);
    virtio::blk blk;
    blk.ring = &ring;
    bio first, second;
    assert(blk.make_request(&first) == 0);
    auto* avail = reinterpret_cast<virtio::vring_avail*>(ring.get_avail_addr());
    auto* used = reinterpret_cast<virtio::vring_used*>(ring.get_used_addr());
    std::thread producer([&] { assert(blk.make_request(&second) == 0); });
    while (!sched::producer_waiting.load()) std::this_thread::yield();
    used->_used_elements[0] = {avail->_ring[0], 1};
    used->_idx.store(1, std::memory_order_release);
    std::thread consumer([&] {
        sched::consumer = true;
        try { blk.req_done(); } catch (sched::finished&) {}
    });
    producer.join();
    consumer.join();
    assert(first.done);
    used->_used_elements[1] = {avail->_ring[1], 1};
    used->_idx.store(2, std::memory_order_release);
    assert(virtio::blk::drain_queue(&ring) == 1);
    ring.get_buf_gc();
    assert(second.done);
    assert(ring.effective_avail_ring_count() == 2);
    std::cout << "PASS: tiny full ring make_request and fallback consumer; all reclaimed\n";
}
