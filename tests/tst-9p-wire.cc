/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Protocol facts (no implementation code imported):
// D: https://github.com/chaos/diod/blob/481911e83337e67b60e039a6e950e532d4681805/protocol.md
// P: https://9fans.github.io/plan9port/man/man9/intro.html
// K: https://github.com/torvalds/linux/blob/v6.12/include/net/9p/9p.h
// D/P supply layouts; K supplies opcode numbers. Bytes below are independently
// derived, not captured traffic. D's sample supplies root QID, ENOENT, hello\n.
// Only framing/body preservation is asserted here. QID/string/count parsing,
// errno mapping and request-dependent validation require operation interfaces.

#include <fs/9p/codec.hh>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

using p9::bytes;
static constexpr uint32_t cap = 65536;
static unsigned checks;

static void require(bool ok, const char* name)
{
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (check %u)\n", name, checks);
        std::exit(1);
    }
}

// Sentinels prove that failed calls leave caller-owned output untouched.
static const p9::frame sentinel_frame{0x5a, 0xa55a, {0xde, 0xad}};
static const bytes sentinel_bytes{0xbe, 0xef, 0x42};

static void decode_fails(const uint8_t* p, size_t len, uint32_t msize,
                         int expected, const char* name)
{
    p9::frame out = sentinel_frame;
    require(p9::decode(p, len, msize, out) == expected, name);
    require(out.type == sentinel_frame.type && out.tag == sentinel_frame.tag
            && out.body == sentinel_frame.body, name);
}

static void encode_fails(const p9::frame& f, uint32_t msize, int expected,
                         const char* name)
{
    bytes out = sentinel_bytes;
    require(p9::encode(f, msize, out) == expected, name);
    require(out == sentinel_bytes, name);
}

// Inputs for encode are independent of decode's output. In particular, an
// encoder/decoder sharing a header/endian bug cannot satisfy the literal wire.
static void vector_case(const char* name, const bytes& wire,
                        const p9::frame& expected, uint32_t msize = cap)
{
    p9::frame decoded{};
    require(p9::decode(wire.data(), wire.size(), msize, decoded) == 0, name);
    require(decoded.type == expected.type && decoded.tag == expected.tag
            && decoded.body == expected.body, name);
    bytes encoded;
    require(p9::encode(expected, msize, encoded) == 0, name);
    require(encoded == wire, name);

    // Every strict prefix is incomplete, including the valid first 7 bytes
    // of a larger frame. decode takes one complete frame, not a TCP stream.
    // Exact-size copies let ASan catch a read past the bytes supplied.
    for (size_t n = 0; n < wire.size(); ++n) {
        const bytes prefix(wire.begin(), wire.begin() + n);
        decode_fails(prefix.data(), n, msize, EPROTO, name);
    }
    // Neither ignore trailing transport bytes nor consume a coalesced frame.
    bytes trailing = wire;
    trailing.push_back(0);
    decode_fails(trailing.data(), trailing.size(), msize, EPROTO, name);
    // A real wire buffer need not be aligned for native integer access.
    bytes unaligned{0xa5};
    unaligned.insert(unaligned.end(), wire.begin(), wire.end());
    require(p9::decode(unaligned.data() + 1, wire.size(), msize, decoded)
            == 0, name);
    require(decoded.type == expected.type && decoded.tag == expected.tag
            && decoded.body == expected.body, name);
    std::printf("PASS: %s\n", name);
}

static void independent_vectors()
{
    // 7 + msize[4] + length[2] + eight dialect bytes = 21 (0x15).
    vector_case("Tversion exact 7-byte header and .L bytes",
        {0x15,0,0,0, 0x64,0xff,0xff, 0,0,1,0, 8,0,
         '9','P','2','0','0','0','.','L'},
        {100, 0xffff, {0,0,1,0, 8,0, '9','P','2','0','0','0','.','L'}});
    vector_case("Rversion exact .L bytes",
        {0x15,0,0,0, 0x65,0xff,0xff, 0,0,1,0, 8,0,
         '9','P','2','0','0','0','.','L'},
        {101, 0xffff, {0,0,1,0, 8,0, '9','P','2','0','0','0','.','L'}});
    vector_case("Tflush non-symmetric little-endian tags",
        {9,0,0,0, 0x6c,0x34,0x12, 0x78,0x56},
        {108, 0x1234, {0x78,0x56}});
    vector_case("Rflush exactly seven bytes",
        {7,0,0,0, 0x6d,2,0}, {109, 2, {}}, 7);
    vector_case("Rlerror ENOENT bytes, not errno conversion",
        {11,0,0,0, 7,1,0, 2,0,0,0}, {7, 1, {2,0,0,0}});
    vector_case("Rwrite count six bytes preserved",
        {11,0,0,0, 0x77,1,0, 6,0,0,0}, {119, 1, {6,0,0,0}});
    vector_case("Rread six-byte independent sample",
        {17,0,0,0, 0x75,1,0, 6,0,0,0, 'h','e','l','l','o','\n'},
        {117, 1, {6,0,0,0, 'h','e','l','l','o','\n'}});
    vector_case("Rread zero count",
        {11,0,0,0, 0x75,1,0, 0,0,0,0}, {117, 1, {0,0,0,0}});
    // D sample directory: type=0x80, version=0, path=0x2c1fac.
    vector_case("Rattach unpadded 13-byte QID body",
        {20,0,0,0, 0x69,1,0, 0x80,0,0,0,0, 0xac,0x1f,0x2c,0,0,0,0,0},
        {105, 1, {0x80,0,0,0,0, 0xac,0x1f,0x2c,0,0,0,0,0}});
    vector_case("Rwalk one QID, partialness needs request context",
        {22,0,0,0, 0x6f,1,0, 1,0, 0x80,0,0,0,0, 0xac,0x1f,0x2c,0,0,0,0,0},
        {111, 1, {1,0, 0x80,0,0,0,0, 0xac,0x1f,0x2c,0,0,0,0,0}});
    // fid=1, newfid=2, one two-byte component. ff 80 is not UTF-8.
    vector_case("Twalk preserves filename bytes, not Unicode",
        {21,0,0,0, 0x6e,1,0, 1,0,0,0, 2,0,0,0, 1,0, 2,0, 0xff,0x80},
        {110, 1, {1,0,0,0, 2,0,0,0, 1,0, 2,0, 0xff,0x80}});
    vector_case("Txattrwalk empty name lists attributes",
        {17,0,0,0, 0x1e,1,0, 1,0,0,0, 2,0,0,0, 0,0},
        {30, 1, {1,0,0,0, 2,0,0,0, 0,0}});
    vector_case("Rreadlink slash allowed in target",
        {12,0,0,0, 0x17,1,0, 3,0, 'a','/','b'},
        {23, 1, {3,0, 'a','/','b'}});
    // dfid=1, name=d, mode=character device 020600=0x2180,
    // major=1, minor=3, gid=1000. Total 7+4+2+1+4+4+4+4=30.
    vector_case("Tmknod representative .L body",
        {30,0,0,0, 0x12,1,0, 1,0,0,0, 1,0,'d', 0x80,0x21,0,0,
         1,0,0,0, 3,0,0,0, 0xe8,3,0,0},
        {18, 1, {1,0,0,0, 1,0,'d', 0x80,0x21,0,0,
                 1,0,0,0, 3,0,0,0, 0xe8,3,0,0}});
    vector_case("Rlock blocked status is not framing errno",
        {8,0,0,0, 0x35,1,0, 1}, {53, 1, {1}});
    // One '.' entry: QID[13]+cookie[8]+DT_DIR[1]+length[2]+name[1]=25.
    vector_case("Rreaddir complete 25-byte record preserved",
        {36,0,0,0, 0x29,1,0, 25,0,0,0,
         0x80,0,0,0,0, 0xac,0x1f,0x2c,0,0,0,0,0,
         1,0,0,0,0,0,0,0, 4,1,0,'.'},
        {41, 1, {25,0,0,0, 0x80,0,0,0,0, 0xac,0x1f,0x2c,0,0,0,0,0,
                 1,0,0,0,0,0,0,0, 4,1,0,'.'}});
}

static void malformed_framing()
{
    // Catch omitted minimum/available-byte/overflow checks. These reject at
    // the envelope; no operation-specific layout assumptions are necessary.
    const bytes malformed[] = {
        {0,0,0,0, 0x6d,1,0},
        {6,0,0,0, 0x6d,1,0},
        {8,0,0,0, 0x6d,1,0},
        {0xff,0xff,0xff,0xff, 0x75,1,0},
        {0,0,0,7, 0x6d,1,0}, // big-endian size must not look like seven
        {7,0,0,0, 0x6d,1,0, 7,0,0,0,0x6d,2,0}, // coalesced frames
        // Self-consistent size == length below the 7-byte header: a guard
        // that only protects the size field must not accept these.
        {4,0,0,0},
        {5,0,0,0, 0x6d},
        {6,0,0,0, 0x6d,1}
    };
    for (const auto& wire : malformed) {
        decode_fails(wire.data(), wire.size(), cap, EPROTO,
                     "malformed envelope");
    }
    const bytes empty_reply{7,0,0,0, 0x6d,1,0};
    for (uint32_t msize : {0u, 6u, 65537u, 0xffffffffu}) {
        decode_fails(empty_reply.data(), empty_reply.size(), msize, EINVAL,
                     "invalid negotiated bound decode");
        encode_fails({109, 1, {}}, msize, EINVAL,
                     "invalid negotiated bound encode");
    }

    // A negotiated msize below the local cap must govern both directions.
    const bytes tversion{0x15,0,0,0, 0x64,0xff,0xff, 0,0,1,0, 8,0,
                         '9','P','2','0','0','0','.','L'};
    p9::frame decoded{};
    decode_fails(tversion.data(), tversion.size(), 20, EPROTO,
                 "decode size above negotiated msize");
    require(p9::decode(tversion.data(), tversion.size(), 21, decoded) == 0,
            "decode size equal to negotiated msize");
    encode_fails({109, 1, {1, 2}}, 8, EMSGSIZE,
                 "encode body above negotiated msize");
    bytes encoded;
    require(p9::encode({109, 1, {1, 2}}, 9, encoded) == 0
            && encoded == bytes({9,0,0,0, 0x6d,1,0, 1,2}),
            "encode body equal to negotiated msize");
    std::puts("PASS: malformed framing and invalid bounds");
}

static void exact_cap()
{
    // Literal prefixes independently establish LE size 0x00010000 and counts
    // 65525 (0xfff5=msize-11), 65513 (0xffe9=msize-23).
    bytes read_wire{0,0,1,0, 0x75,1,0, 0xf5,0xff,0,0};
    read_wire.resize(65536, 0xa5);
    bytes read_body{0xf5,0xff,0,0};
    read_body.resize(65529, 0xa5);
    // Avoid the every-prefix loop for these large fixtures.
    p9::frame decoded{};
    bytes encoded;
    require(p9::decode(read_wire.data(), read_wire.size(), cap, decoded) == 0
            && decoded.type == 117 && decoded.tag == 1
            && decoded.body == read_body, "Rread exactly 65536 decode");
    require(p9::encode({117, 1, read_body}, cap, encoded) == 0
            && encoded == read_wire, "Rread exactly 65536 encode");

    bytes write_wire{0,0,1,0, 0x76,1,0, 1,0,0,0,
                     8,7,6,5,4,3,2,1, 0xe9,0xff,0,0};
    write_wire.resize(65536, 0x5a);
    bytes write_body{1,0,0,0, 8,7,6,5,4,3,2,1, 0xe9,0xff,0,0};
    write_body.resize(65529, 0x5a);
    require(p9::decode(write_wire.data(), write_wire.size(), cap, decoded) == 0
            && decoded.type == 118 && decoded.tag == 1
            && decoded.body == write_body, "Twrite exactly 65536 decode");
    require(p9::encode({118, 1, write_body}, cap, encoded) == 0
            && encoded == write_wire, "Twrite exactly 65536 encode");

    // Complete internally consistent 65537-byte envelopes, not just a short
    // buffer advertising too many bytes. Increase counts as well as size.
    read_wire[0] = 1;
    read_wire[7] = 0xf6;
    read_wire.push_back(0xa5);
    read_body[0] = 0xf6;
    read_body.push_back(0xa5);
    write_wire[0] = 1;
    write_wire[19] = 0xea;
    write_wire.push_back(0x5a);
    write_body[12] = 0xea;
    write_body.push_back(0x5a);
    decode_fails(read_wire.data(), read_wire.size(), cap, EPROTO,
                 "Rread 65537 rejected");
    decode_fails(write_wire.data(), write_wire.size(), cap, EPROTO,
                 "Twrite 65537 rejected");
    encode_fails({117, 1, read_body}, cap, EMSGSIZE,
                 "Rread encode one over cap rejected");
    encode_fails({118, 1, write_body}, cap, EMSGSIZE,
                 "Twrite encode one over cap rejected");
    std::puts("PASS: exact 65536 and complete one-over envelopes");
}

int main()
{
    independent_vectors();
    malformed_framing();
    exact_cap();
    std::printf("SUMMARY: %u framing checks, 0 failures; "
                "operation validation NOT covered\n", checks);
    return 0;
}
