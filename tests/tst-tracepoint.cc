/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/trace.hh>
#include <osv/debug.hh>

struct test_object {
    int i = 1;
    long j = 2;
    static std::tuple<int, long> unpack(test_object& obj) {
        return std::make_tuple(obj.i, obj.j);
    }
};



tracepoint<10001, unsigned, long> trace_1("tp1", "%d %d");
tracepointv<10002, decltype(test_object::unpack), test_object::unpack>
    trace_2("tp2", "%d %d");
tracepoint<10003, const char*, long, const char*> trace_string("tp3", "%s %d %s");


std::string signature_string(u64 s)
{
    std::string ret;
    while (s) {
        ret.push_back(s & 255);
        s >>= 8;
    }
    return ret;
}

int main(int ac, char** av)
{
    test_object obj;
    trace_1(10, 20);
    debug(boost::format("trace_1 signature: %s") % signature_string(trace_1.signature()));
    assert(signature_string(trace_1.signature()) == "Iq");
    struct {
        u32 a0;
        s64 a1;
    } tmp = {};
    auto args = std::make_tuple(u32(10), s64(20));
    trace_1.serialize(&tmp, args);
    auto size = trace_1.size(args);
    assert(size == 16 && tmp.a0 == 10 && tmp.a1 == 20);
    trace_2(obj);
    trace_string("foo", 6, "bar");
}

