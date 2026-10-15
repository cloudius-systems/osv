/* Copyright (C) 2026 Greg Burd. */
#include "crucible-messages.hh"
#include <cassert>
#include <limits>
int main() {
    crucible::bincode::Encoder e;
    e.encode_u64(std::numeric_limits<uint64_t>::max());
    auto bytes = e.data();
    bool refused = false;
    try {
        crucible::bincode::Decoder d(bytes);
        d.skip(8);
        d.skip(std::numeric_limits<size_t>::max());
    } catch (const std::runtime_error&) { refused = true; }
    assert(refused);
    refused = false;
    try {
        crucible::bincode::Decoder d(bytes);
        d.decode_vec<uint64_t>([&] { return d.decode_u64(); });
    } catch (const std::length_error&) {
        // reserve() saw wire-controlled count: not bounded by frame.
        assert(false);
    } catch (const std::runtime_error&) { refused = true; }
    assert(refused);
    for (unsigned value : {2u, 255u}) {
        std::vector<uint8_t> input{static_cast<uint8_t>(value)};
        refused = false;
        try { crucible::bincode::Decoder d(input); d.decode_bool(); }
        catch (const std::runtime_error&) { refused = true; }
        assert(refused);
    }
}
