/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/trace.hh>
#include <osv/net_trace.hh>
#include <vector>
#include <iterator>

class mbuf_iterator : public std::iterator<std::input_iterator_tag, char, size_t> {
private:
    struct mbuf* _m;
    size_t _pos;
private:
    void ensure_next()
    {
        while (_m != nullptr && _pos == (size_t) _m->m_hdr.mh_len) {
            _m = _m->m_hdr.mh_next;
            _pos = 0;
        }
    }
public:
    mbuf_iterator(struct mbuf* m)
        : _m(m)
        , _pos(0)
    {
        ensure_next();
    }

    void operator++()
    {
        _pos++;
        ensure_next();
    }

    mbuf_iterator operator+(size_t delta) const
    {
        mbuf_iterator new_iterator(*this);
        new_iterator += delta;
        return new_iterator;
    }

    void operator+=(size_t delta)
    {
        while (_m != nullptr && delta > 0) {
            auto step = std::min(delta, _m->m_hdr.mh_len - _pos);
            _pos += step;
            delta -= step;
            ensure_next();
        }
    }

    char& operator*()
    {
        return _m->m_hdr.mh_data[_pos];
    }

    char* operator->()
    {
        return &_m->m_hdr.mh_data[_pos];
    }

    bool operator==(const mbuf_iterator& other) const
    {
        return other._m == _m && other._pos == _pos;
    }

    bool operator!=(const mbuf_iterator& other) const
    {
        return !(*this == other);
    }

    friend size_t std::distance<mbuf_iterator>(mbuf_iterator, mbuf_iterator);
};

namespace std {

template<>
size_t distance<mbuf_iterator>(mbuf_iterator first, mbuf_iterator second)
{
    if (first._m == second._m) {
        return second._pos - first._pos;
    }

    size_t distance = 0;
    auto* m = first._m;

    if (m) {
        distance += m->m_hdr.mh_len - first._pos;
    }

    m = m->m_hdr.mh_next;

    while (m && m != second._m) {
        distance += m->m_hdr.mh_len;
        m = m->m_hdr.mh_next;
    }

    if (m) {
        distance += second._pos;
    }

    return distance;
}

}

template<size_t limit>
class mbuf_slice : public blob_tag {
private:
    struct mbuf* _m;
public:
    using iterator = mbuf_iterator;

    mbuf_slice(struct mbuf* m)
        : _m(m)
    {
    }

    iterator begin() const
    {
        return mbuf_iterator(_m);
    }

    iterator end() const
    {
        return begin() + limit;
    }
};

static constexpr size_t capture_limit = 128;
using slice_t = mbuf_slice<capture_limit>;

TRACEPOINT(trace_net_packet_in, "proto=%d, data=%s", int, slice_t);
TRACEPOINT(trace_net_packet_out, "proto=%d, data=%s", int, slice_t);
TRACEPOINT(trace_net_packet_handling, "proto=%d, data=%s", int, slice_t);

void log_packet_in(struct mbuf* m, int proto)
{
    trace_net_packet_in(proto, slice_t(m));
}

void log_packet_out(struct mbuf* m, int proto)
{
    trace_net_packet_out(proto, slice_t(m));
}

void log_packet_handling(struct mbuf* m, int proto)
{
    trace_net_packet_handling(proto, slice_t(m));
}
