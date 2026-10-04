/* Copyright (C) 2026 Greg Burd. */
#include "crucible-config.hh"
#include <cassert>
#include <stdexcept>
int main() {
    assert(crucible::parse_generation("1") == 1);
    assert(crucible::parse_generation("18446744073709551615") == UINT64_MAX);
    for (auto s : {"", "0", "-1", "+1", " 1", "1x", "18446744073709551616"}) {
        bool refused = false;
        try { crucible::parse_generation(s); }
        catch (const std::exception&) { refused = true; }
        assert(refused);
    }
}
