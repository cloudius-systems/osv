import mmap
import struct
import heapq
import bisect

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
        if not self.begin:
            begin = other.begin
        elif not other.begin:
            begin = self.begin
        else:
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
    def __init__(self, tp, thread, time, cpu, data, backtrace=None):
        self.tp = tp
        self.thread = thread
        self.time = time
        self.cpu = cpu
        self.data = data
        self.backtrace = backtrace

    @property
    def thread_name(self):
        return self.thread.name

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
            self.thread.ptr,
            self.thread.name,
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
        self.duration_ = duration

    @property
    def duration(self):
        return self.duration_

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

def do_split_format(format_str):
    chars = iter(format_str)
    while True:
        try:
            c = next(chars)
        except StopIteration:
            return
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

_split_cache = {}
def split_format(format_str):
    if not format_str:
        return []
    result = _split_cache.get(format_str, None)
    if not result:
        result = list(do_split_format(format_str))
        _split_cache[format_str] = result
    return result

formatters = {
    '*': lambda bytes: '{' + ' '.join('%02x' % b for b in bytes) + '}'
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
                if fmt.startswith('50p'):
                   values.append(val.decode('utf-8'))
                else:
                   values.append(val)

        return tuple(values)

    def __bool__(self):
        return self.offset < len(self.buffer)

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
                if fmt == '50p':
                    self.writer(struct.pack(fmt, arg.encode('utf-8')))
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

class NotATraceDumpFile(Exception):
    pass

class TraceDumpReaderBase :
    def __init__(self, filename):
        self.endian = '<'
        self.file = open(filename, 'rb')
        try:
            tag = self.file.read(4).decode()
            if tag == "OSVT":
                endian = '>'
            elif tag != "TVSO":
                raise NotATraceDumpFile("Not a trace dump file")
            self.read('Q') # size. ignore, do not support embedded yet.
            if self.read('I') != 1: #endian check. verify tag check
                raise SyntaxError
            self.read('I') # version. should check
            while self.readStruct0():
                pass
        finally:
            self.file.close()

    def align(self, a):
        while (self.file.tell() & (a - 1)) != 0:
            self.file.seek(1, 1)

    def read0(self, type):
        siz = struct.calcsize(type)
        self.align(siz)
        val = self.file.read(siz)
        if len(val) != siz:
            raise EOFError(str(len(val)) + "!=" + str(siz))
        return val

    def read(self, type):
        return struct.unpack(self.endian + type, self.read0(type))[0]

    def skip(self, types):
        for type in types:
            self.read0(type)

    def readStruct(self, tag, size):
        return False

    def readStruct0(self):
        self.align(8)
        try:
            tag = self.read('I')
        except EOFError:
            return False
        size = self.read('Q')
        if not self.readStruct(tag, size):
            self.file.seek(size, 1)
        return True

    def readString(self):
        len = self.read('H')
        return self.file.read(len).decode()

class TraceDumpReader(TraceDumpReaderBase) :
    def __init__(self, filename):
        self.tracepoints = {}
        self.trace_buffers = []
        TraceDumpReaderBase.__init__(self, filename)

    def readStruct(self, tag, size):
        if tag == 0x54524344: # 'TRCD'
            return self.readTraceDict(size)
        elif tag == 0x54524353: #'TRCS'
            data = self.file.read(size)
            self.trace_buffers.append(data)
            return True
        else:
            return False

    def readTraceDict(self, size):
        self.backtrace_len = self.read('I')
        n_types = self.read('I')
        for i in range(0, n_types):
            tp_key = self.read('Q')
            id = self.readString()
            name = self.readString()
            prov = self.readString()
            fmt = self.readString()
            n_args = self.read('I')
            sig = ""
            for j in range(0, n_args):
                arg_name = self.readString()
                arg_sig = self.file.read(1).decode()
                if arg_sig == 'p':
                    arg_sig = '50p'
                sig += arg_sig
            tp = TracePoint(tp_key, name, sig, fmt)
            self.tracepoints[tp_key] = tp
        return True

    def oneTrace(self, trace_log):
        last_tp = None
        last_trace = None
        unpacker = SlidingUnpacker(trace_log)
        while unpacker:
            tp_key, = unpacker.unpack('Q')
            if (tp_key == 0) or (tp_key == -1):
                break

            thread, thread_name, time, cpu, flags = unpacker.unpack('Q16sQII')

            tp = self.tracepoints.get(tp_key, None)
            if not tp:
                raise SyntaxError(("Unknown trace point 0x%x" % tp_key))

            thread_name = thread_name.partition(b'\0')[0].decode()

            backtrace = None
            if flags & 1:
                backtrace = [_f for _f in unpacker.unpack('Q' * self.backtrace_len) if _f]

            data = unpacker.unpack(tp.signature)
            unpacker.align_up(8)
            last_tp = tp
            last_trace = Trace(tp, Thread(thread, thread_name), time, cpu, data, backtrace=backtrace)
            yield last_trace

    def traces(self):
        iters = [self.oneTrace(data) for data in self.trace_buffers]
        return heapq.merge(*iters)


class Symbol:
    def __init__(self, addr, size, name=None, filename=None, line=None):
        self.addr = addr
        self.size = size
        self.name = name
        self.filename = filename
        self.size = size
        self.line = line

    def __lt__(self, b):
        return self.addr < b.addr


class TraceDumpSymbols(TraceDumpReaderBase) :
    def __init__(self, filename, delegate = debug.DummyResolver()):
        self.delegate = delegate
        self.symbols = []
        self.modules = []
        self.segments = []
        self.cache = {}
        TraceDumpReaderBase.__init__(self, filename)
        self.symbols.sort()
        self.modules.sort()
        self.segments.sort()

    def readStruct(self, tag, size):
        if tag == 0x53594D42: # 'SYMB'
            return self.readSymbols(size)
        elif tag == 0x4D4F4453: #'MODS'
            return self.readModules(size)
        else:
            return False

    def readSymbols(self, size):
        n_symbols = self.read('I')
        for i in range(0, n_symbols):
            name = self.readString()
            addr = self.read('Q')
            size = self.read('Q')
            file = self.readString()
            n_lines = self.read('I')
            for j in range(0, n_lines):
                offs = self.read('I')
                line = self.read('I')
            self.symbols.append(Symbol(addr, size, name, file))
        return True

    def readModules(self, size):
        n_modules = self.read('I')
        for i in range(0, n_modules):
            file = self.readString()
            base = self.read('Q')
            size = self.read('Q')
            self.modules.append(Symbol(base, size, file))
            n_segments = self.read('I')
            for j in range(0, n_segments):
                name = self.readString()
                self.skip('IIQ')
                addr = self.read('Q')
                offs = self.read('Q')
                size = self.read('Q')
                self.segments.append(Symbol(addr, size, name, file))
        return True

    def __call__(self, addr):
        result = self.cache.get(addr, None)
        if result:
            return result
        x = debug.SourceAddress(addr, 0)
        for a in [self.symbols, self.segments, self.modules]:
            index = bisect.bisect_left(a, x) - 1
            if index > 0 and index < len(a):
                s = a[index]
                if s.addr <= addr and (s.addr + s.size) > addr:
                    result = [debug.SourceAddress(addr, ('%s+0x%x (%#08x)' % (s.name, addr - s.addr, addr)), s.filename, s.line)]
                    self.cache[addr] = result
                    return result
        result = self.delegate(addr)
        self.cache[addr] = result
        return result

class Thread(object):
    def __init__(self, ptr, name):
        self.ptr = ptr
        self.name = name

    def __str__(self):
        return "%s (0x%x)" % (self.name, self.ptr)

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
        tp_key, thread_ptr, thread_name, time, cpu = unpacker.unpack('QQ16sQI')
        thread_name = thread_name.rstrip(b'\0').decode('utf-8')
        tp = tracepoints[tp_key]

        backtrace = []
        while True:
            frame, = unpacker.unpack('Q')
            if not frame:
                break
            backtrace.append(frame)

        data = unpacker.unpack(tp.signature)
        yield Trace(tp, Thread(thread_ptr, thread_name), time, cpu, data, backtrace=backtrace)

def write(traces, writer):
    packer = WritingPacker(writer)
    packer.pack('i', _format_version)

    tracepoints = set(trace.tp for trace in traces)
    packer.pack('Q', len(tracepoints))
    for tp in tracepoints:
        packer.pack('Q', tp.key)
        packer.pack_str(tp.name, tp.signature, tp.format)

    for trace in traces:
        packer.pack('QQ16sQI', trace.tp.key, trace.thread.ptr, trace.thread.name.encode(),
                    trace.time, trace.cpu)

        if trace.backtrace:
            for frame in [_f for _f in trace.backtrace if _f]:
                packer.pack('Q', frame)
        packer.pack('Q', 0)

        packer.pack(trace.tp.signature, *trace.data)

class read_file:
    def __init__(self, filename):
        self.filename = filename
        self.map = None
        self.dumpreader = None

    def __enter__(self):
        try:
            self.dumpreader = TraceDumpReader(self.filename)
        except NotATraceDumpFile:
            self.file = open(self.filename, 'r+b')
            self.map = mmap.mmap(self.file.fileno(), 0)

        return self

    def __exit__(self, *args):
        if self.map:
            self.map.close()
            self.file.close()

    def get_traces(self):
        if self.dumpreader:
            return self.dumpreader.traces()
        return read(self.map)

def write_to_file(filename, traces):
    with open(filename, 'wb') as file:
        return write(traces, file.write)
