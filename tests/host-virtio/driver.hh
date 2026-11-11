// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
namespace virtio {
class virtio_driver {
public:
    size_t get_vring_alignment() { return 4096; }
    bool get_indirect_buf_cap() { return true; }
    bool get_event_idx_cap() { return false; }
    bool kick(unsigned) { return true; }
};
}
