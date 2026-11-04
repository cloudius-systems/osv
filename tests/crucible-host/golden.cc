/*
 * Copyright (C) 2026 Greg Burd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// Driver encoders/decoders against frames from upstream's own CrucibleEncoder.
#include "crucible-messages.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
using namespace crucible;
static std::map<std::string, std::vector<uint8_t>> golden;
static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}
static const std::vector<uint8_t>& vec(const char* name) {
    auto it = golden.find(name);
    if (it == golden.end()) { std::fprintf(stderr, "missing vector %s\n", name); std::exit(2); }
    return it->second;
}
template<typename T> static T decode(const char* name, MessageType type) {
    auto& frame = vec(name);
    check(frame.size() >= 8 && frame[0] + (frame[1] << 8) + (frame[2] << 16) +
          (uint32_t(frame[3]) << 24) == frame.size(), name);
    std::vector<uint8_t> payload(frame.begin() + 4, frame.end());
    bincode::Decoder dec(payload);
    check(decode_message_type(dec) == type, name);
    T msg = T::decode(dec);
    check(dec.at_end(), name);
    return msg;
}
int main(int argc, char** argv) {
    std::ifstream in(argc > 1 ? argv[1] : "golden-v14.txt");
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string name, hex;
        fields >> name >> hex;
        auto& out = golden[name];
        for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    }
    const uint8_t up[16] = {1, 2, 3, 4, 5, 6, 0x47, 8, 0x89, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t ses[16] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x47, 0x18,
                             0x99, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};
    Uuid u, s;
    std::memcpy(u.bytes, up, 16);
    std::memcpy(s.bytes, ses, 16);
    // Encode exactly what the driver puts on the wire, compare byte for byte.
    auto here = [&](uint32_t v, bool ro, std::vector<uint32_t> alts) {
        HereIAm m;
        m.version = v; m.upstairs_id = u; m.session_id = s; m.gen = 7;
        m.read_only = ro; m.encrypted = false; m.alternate_versions = alts;
        return encode_message(m);
    };
    check(here(14, false, {13}) == vec("here_i_am_v14_alt13"), "HereIAm V14 [13]");
    check(here(14, true, {13}) == vec("here_i_am_v14_alt13_ro"), "HereIAm V14 [13] read-only");
    check(here(13, false, {}) == vec("here_i_am_v13"), "HereIAm V13 []");
    check(encode_message(BeenActivePlease{}) == vec("been_active_please"), "BeenActivePlease");
    check(encode_message(YouHaveBeenActive{u, s}) == vec("you_have_been_active"), "YouHaveBeenActive");
    check(encode_message(ExtentVersionsPlease{}) == vec("extent_versions_please"), "ExtentVersionsPlease");
    check(!decode<BeenActive>("been_active_false", MessageType::BeenActive).been_active, "BeenActive false");
    check(decode<BeenActive>("been_active_true", MessageType::BeenActive).been_active, "BeenActive true");
    auto y14 = decode<YesItsMe>("yes_its_me_v14", MessageType::YesItsMe);
    check(y14.version == 14 && y14.repair_addr.port == 4567, "YesItsMe V14");
    auto y13 = decode<YesItsMe>("yes_its_me_v13", MessageType::YesItsMe);
    check(y13.version == 13 && y13.repair_addr.port == 4567, "YesItsMe V13");
    check(decode<VersionMismatch>("version_mismatch_14", MessageType::VersionMismatch).offered == 14,
          "VersionMismatch 14");
    if (failures) return 1;
    std::printf("PASS golden V14/V13 negotiation frames (%zu vectors)\n", golden.size());
}
