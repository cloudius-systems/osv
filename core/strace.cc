/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/strace.hh>
#include <osv/sched.hh>
#include "drivers/console.hh"

trace_log* _trace_log = nullptr;

static void print_trace(trace_record* tr) {
    char msg[512];
    auto tp = tr->tp;
    float time = tr->time;
    std::string thread_name = tr->thread_name.data();

    auto len = snprintf(msg, 512, "%-15s %3d %12.9f %s(", thread_name.c_str(), tr->cpu, time / 1000000000, tp->name);
    auto left = 512 - len;
    auto m = msg + len;

    auto fmt = tp->format;
    //Copy all up to 1st '%'
    while (*fmt && *fmt != '%' && left > 0) {
       *m++ = *fmt++;
       left--;
       len++;
    }

    auto buf = tr->buffer;
    auto sig = tr->tp->sig;
    int written = 0;

    if (tr->backtrace) {
        buf += tracepoint_base::backtrace_len * sizeof(void*);
    }

    while (*sig != 0 && left > 2) {
        //Copy fragment of tp->format up to next '%'
        char _fmt[128];
        int i = 0;
        do {
           _fmt[i++] = *fmt++;
        } while (*fmt && *fmt != '%');
        _fmt[i] = 0;

        //Detect type of data, deserialize and print to the msg
        switch (*sig++) {
        case 'c':
            buf = align_up(buf, object_serializer<char>().alignment());
            written = snprintf(m, left, _fmt,  *reinterpret_cast<char*>(buf++));
            break;
        case 'b':
        case 'B':
            buf = align_up(buf, object_serializer<u8>().alignment());
            written = snprintf(m, left, _fmt,  *buf++);
            break;
        case 'h':
        case 'H':
            buf = align_up(buf, object_serializer<u16>().alignment());
            written = snprintf(m, left, _fmt,  *reinterpret_cast<u16*>(buf));
            buf += sizeof(u16);
            break;
        case 'i':
        case 'I':
        case 'f':
            buf = align_up(buf, object_serializer<u32>().alignment());
            written = snprintf(m, left, _fmt,  *reinterpret_cast<u32*>(buf));
            buf += sizeof(u32);
            break;
        case 'q':
        case 'Q':
        case 'd':
        case 'P':
            buf = align_up(buf, object_serializer<u64>().alignment());
            written = snprintf(m, left, _fmt,  *reinterpret_cast<u64*>(buf));
            buf += sizeof(u64);
            break;
        case '?':
            written = snprintf(m, left, _fmt,  *reinterpret_cast<bool*>(buf++));
            break;
        case 'p': {
            //string
            char str[128];
            auto slen = *buf++;
            int i = 0;
            while (slen-- && i < 127) {
                str[i++] = *reinterpret_cast<char*>(buf++);
            }
            str[i] = 0;
            written = snprintf(m, left, _fmt, str);
            buf += (object_serializer<const char*>::max_len - i - 1);
            break;
        }
        case '*': {
            //binary data
            buf = align_up(buf, sizeof(u16));
            char str[256];
            auto slen = *reinterpret_cast<u16*>(buf);
            int i = 0;
            buf += 2;
            str[i++] = '{';
            while (slen-- && (i + 4) < 255) {
                auto byte = *reinterpret_cast<u8*>(buf++);
                auto high = byte / 16;
                str[i++] = high < 10 ? '0' + high : 'a' + (high - 10);
                auto low = byte % 16;
                str[i++] = low < 10 ? '0' + low : 'a' + (low - 10);
                str[i++] = ' ';
            }
            str[i++] = '}';
            str[i] = 0;
            written = snprintf(m, left, _fmt, str);
            break;
        }
        default:
            assert(0 && "should not reach");
        }

        left -= written;
        len += written;
        m += written;
    }
    *m++ = ')';
    *m++ = '\n';
    console::write(msg, len + 2);
}

static sched::thread *strace = nullptr;
static std::atomic<bool> strace_done = {false};

static void print_traces() {
    while (auto tr = _trace_log->read()) {
        print_trace(tr);
    }
}

void start_strace() {
    _trace_log = new trace_log();
    strace = sched::thread::make([] {
        print_traces();
        do {
            sched::thread::sleep(std::chrono::microseconds(100));
            print_traces();
        } while (!strace_done);
    }, sched::thread::attr().name("strace"));

    strace->start();
}

void wait_strace_complete() {
    if (!_trace_log) {
        return;
    }
    strace_done = true;
    strace->join();
    delete strace;
}
