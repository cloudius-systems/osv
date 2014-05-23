import os
import mmap
import struct
import sys
from osv import debug

# version 2 introduced thread_name
# version 3 introduced variable-length arguments (blob)
_format_version = 3

def nanos_to_millis(nanos):
    return float(nanos) / 1000000

def nanos_to_seconds(nanos):
    return float(nanos) / 1000000000

def format_duration(time):
    return "%4.3f" % nanos_to_millis(time)

def format_time(time):
    return "%12.9f" % nanos_to_seconds(time)

class BacktraceFormatter:
    def __init__(self, resolver, formatter):
        self.resolver = resolver
        self.formatter = formatter

    def __call__(self, backtrace):
        if not backtrace:
            return ''

        frames = list(debug.resolve_all(self.resolver, (x - 1 for x in backtrace if x)))

        while frames[0].name and frames[0].name.startswith("tracepoint"):
            frames.pop(0)

        return '   [' + ', '.join(map(self.formatter, frames)) + ']'

def simple_symbol_formatter(src_addr):
    return '0x%x' % src_addr.addr

default_backtrace_formatter = BacktraceFormatter(debug.DummyResolver(), simple_symbol_formatter)

class TimeRange(object):
    """
    Represents time from @begin inclusive to @end exclusive.
    None in any of these means open range from that side.

    """
    def __init__(self, begin, end):
        self.begin = begin
        self.end = end

    def __contains__(self, timestamp):
        if self.begin and timestamp < self.begin:
            return False
        if self.end and timestamp >= self.end:
            return False
        return True

    def length(self):
        if self.begin is None or self.end is None:
            return None
        return self.end - self.begin

    def intersection(self, other):
        begin = max(self.begin, other.begin)

        if self.end is None:
            end = other.end
        elif other.end is None:
            end = self.end
        else:
            end = min(self.end, other.end)

        if begin and end and begin > end:
            return None

        return TimeRange(begin, end)

class TracePoint:
    def __init__(self, key, name, signature, format):
        self.key = key
        self.name = name
        self.signature = signature
        self.format = format

    def __repr__(self):
        return 'TracePoint(key=%s, name=%s, signature=%s, format=%s)' % (
                self.key,
                self.name,
                self.signature,
                self.format)

class Trace:
    def __init__(self, tp, thread, thread_name, time, cpu, data, backtrace=None):
        self.tp = tp
        self.thread = thread
        self.thread_name = thread_name
        self.time = time
        self.cpu = cpu
        self.data = data
        self.backtrace = backtrace

    @property
    def name(self):
        return self.tp.name

    @staticmethod
    def format_data(sample):
        format = sample.tp.format
        format = format.replace('%p', '0x%016x')
        data = [get_formatter(fmt)(arg) for fmt, arg in zip(split_format(sample.tp.signature), sample.data)]
        return format % tuple(data)

    def format(self, bt_formatter=default_backtrace_formatter, data_formatter=None):
        if not data_formatter:
            data_formatter = self.format_data

        return '0x%016x %-15s %2d %19s %-20s %s%s' % (
            self.thread,
            self.thread_name,
            self.cpu,
            format_time(self.time),
            self.name,
            data_formatter(self),
            bt_formatter(self.backtrace))

    def __str__(self):
        return self.format()

    def __cmp__(self, b):
        return cmp(self.time, b.time)

    def __lt__(self, b):
        return self.time < b.time


class TimedTrace:
    def __init__(self, trace, duration=None):
        self.trace = trace
        self.duration = duration

    @property
    def duration(self):
        return self.duration

    @property
    def time(self):
        return self.trace.time

    @property
    def time_range(self):
        return TimeRange(self.trace.time, self.trace.time + self.duration)

def align_down(v, pagesize):
    return v & ~(pagesize - 1)

def align_up(v, pagesize):
    return align_down(v + pagesize - 1, pagesize)

def split_format(format_str):
    chars = iter(format_str)
    while True:
        c = next(chars)
        if c in '<>=!@':
            raise Exception('Not supported: ' + c)
        if c == '*':
            yield c
        else:
            fmt = str(c)
            while c.isdigit():
                c = next(chars)
                fmt += str(c)
            yield fmt

formatters = {
    '*': lambda bytes: '{' + ' '.join('%02x' % ord(b) for b in bytes) + '}'
}

def get_alignment_of(fmt):
    if fmt == '*':
        return 2
    return struct.calcsize('c' + fmt) - struct.calcsize(fmt)

def get_formatter(format_code):
    def identity(x):
        return x

    return formatters.get(format_code, identity)

class SlidingUnpacker:
    def __init__(self, buffer, offset=0):
        self.buffer = buffer
        self.offset = offset

    def align_up(self, alignment):
        self.offset = align_up(self.offset, alignment)

    def unpack_blob(self):
        len_size = struct.calcsize('H')
        len, = struct.unpack_from('H', self.buffer[self.offset:self.offset+len_size])
        self.offset += len_size
        blob, = struct.unpack('%ds' % len, self.buffer[self.offset:self.offset+len])
        self.offset += len
        return blob

    def unpack_str(self):
        return self.unpack_blob().decode()

    def unpack(self, format):
        values = []
        start_offset = self.offset

        for fmt in split_format(format):
            delta = self.offset - start_offset
            padding = align_up(delta, get_alignment_of(fmt)) - delta
            self.offset += padding

            if fmt == '*':
                values.append(self.unpack_blob())
            else:
                size = struct.calcsize(fmt)
                val, = struct.unpack_from(fmt, self.buffer[self.offset:self.offset+size])
                self.offset += size
                values.append(val)

        return tuple(values)

    def __nonzero__(self):
        return self.offset < len(self.buffer)

    # Python3
    def __bool__(self):
        return self.__nonzero__()

class WritingPacker:
    def __init__(self, writer):
        self.writer = writer
        self.offset = 0

    def pack(self, format, *data):
        args = iter(data)
        start_offset = self.offset
        for fmt in split_format(format):
            arg = next(args)

            alignment = get_alignment_of(fmt)
            delta = self.offset - start_offset
            padding = align_up(delta, alignment) - delta
            if padding:
                self.writer(b'\0' * padding)
                self.offset += padding

            if fmt == '*':
                self.pack_blob(arg)
            else:
                self.writer(struct.pack(fmt, arg))
                self.offset += struct.calcsize(fmt)

    def pack_blob(self, arg):
        count = len(arg)
        self.writer(struct.pack('H', count))
        try:
            self.writer(struct.pack('%ds' % count, arg))
        except:
            print(type(arg))
            print(arg)
            raise
        self.offset += 2 + count

    def pack_str(self, *args):
        for arg in args:
            if not type(arg) == str:
                raise Exception('Should be string but is %s' % type(arg))
            self.pack_blob(arg.encode())

def read(buffer_view):
    unpacker = SlidingUnpacker(buffer_view)
    version, = unpacker.unpack('i')

    if version != _format_version:
        raise Exception('Version mismatch, current is %d got %d' % (_format_version, version))

    tracepoints = {}
    n_tracepoints, = unpacker.unpack('Q')
    for i in range(n_tracepoints):
        key, = unpacker.unpack('Q')
        tracepoints[key] = TracePoint(key, unpacker.unpack_str(),
            unpacker.unpack_str(), unpacker.unpack_str())

    while unpacker:
        tp_key, thread, thread_name, time, cpu = unpacker.unpack('QQ16sQI')
        thread_name = thread_name.rstrip('\0')
        tp = tracepoints[tp_key]

        backtrace = []
        while True:
            frame, = unpacker.unpack('Q')
            if not frame:
                break
            backtrace.append(frame)

        data = unpacker.unpack(tp.signature)
        yield Trace(tp, thread, thread_name, time, cpu, data, backtrace=backtrace)

def write(traces, writer):
    packer = WritingPacker(writer)
    packer.pack('i', _format_version)

    tracepoints = set(trace.tp for trace in traces)
    packer.pack('Q', len(tracepoints))
    for tp in tracepoints:
        packer.pack('Q', tp.key)
        packer.pack_str(tp.name, tp.signature, tp.format)

    for trace in traces:
        packer.pack('QQ16sQI', trace.tp.key, trace.thread, trace.thread_name.encode(),
                    trace.time, trace.cpu)

        if trace.backtrace:
            for frame in filter(None, trace.backtrace):
                packer.pack('Q', frame)
        packer.pack('Q', 0)

        packer.pack(trace.tp.signature, *trace.data)

class read_file:
    def __init__(self, filename):
        self.filename = filename
        self.map = None

    def __enter__(self):
        self.file = open(self.filename, 'r+b')
        self.map = mmap.mmap(self.file.fileno(), 0)
        return self

    def __exit__(self, *args):
        if self.map:
            self.map.close()
        self.file.close()

    def get_traces(self):
        return read(self.map)

def write_to_file(filename, traces):
    with open(filename, 'wb') as file:
        return write(traces, file.write)
