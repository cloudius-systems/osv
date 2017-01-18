#!/usr/bin/env python2
import sys
import errno
import argparse
import re
import os
import math
import subprocess
import requests

from collections import defaultdict

from osv import trace, debug, prof
from osv.client import Client
import memory_analyzer

class InvalidArgumentsException(Exception):
    def __init__(self, message):
        self.message = message

class symbol_printer:
    def __init__(self, resolver, formatter):
        self.resolver = resolver
        self.formatter = formatter

    def __call__(self, addr):
        return self.formatter(self.resolver(addr))

class src_addr_formatter:
    def __init__(self, args):
        self.args = args

    def __call__(self, src_addr):
        if not src_addr.name:
            return '0x%x' % src_addr.addr

        text = src_addr.name

        if self.args.show_file_name:
            text += ' %s' % src_addr.filename

        if self.args.show_line_number:
            text += ':%s' % src_addr.line

        if self.args.show_address:
            text += ' @ 0x%x' % src_addr.addr

        return text

def add_trace_source_options(parser):
    parser.add_argument("tracefile", help="Path to trace file", nargs='?', default="tracefile")

def get_trace_reader(args):
    return trace.read_file(args.tracefile)

def add_symbol_resolution_options(parser):
    group = parser.add_argument_group('symbol resolution')
    group.add_argument("-d", "--debug", action="store_true", help="use loader.elf from debug build")
    group.add_argument("-e", "--exe", action="store", help="path to the object file used for symbol resolution")
    group.add_argument("-x", "--no-resolve", action='store_true', help="do not resolve symbols")
    group.add_argument("--no-inlined-by", action='store_true', help="do not show inlined-by functions")
    group.add_argument("-L", "--show-line-number", action='store_true', help="show line numbers")
    group.add_argument("-A", "--show-address", action='store_true', help="show raw addresses")
    group.add_argument("-F", "--show-file-name", action='store_true', help="show file names")

class BeautifyingResolver(object):
    def __init__(self, delegate):
        self.delegate = delegate

    def __call__(self, addr):
        resolution = self.delegate(addr)
        for src_addr in resolution:
            if src_addr.name:
                if src_addr.name.startswith("void sched::thread::do_wait_until<"):
                    src_addr.name = "sched::thread::do_wait_until"
                elif src_addr.name.startswith("do_wait_until<"):
                    src_addr.name = "do_wait_until"
        return resolution

def symbol_resolver(args):
    if args.no_resolve:
        return debug.DummyResolver()

    if args.exe:
        elf_path = args.exe
    elif args.debug:
        elf_path = 'build/debug/loader.elf'
    else:
        elf_path = 'build/release/loader.elf'

    base = debug.DummyResolver()
    try:
        base = trace.TraceDumpSymbols(args.tracefile, base)
    except trace.NotATraceDumpFile:
        # not a trace dump file. Assume trace buffer file, continue as usual
        pass

    return BeautifyingResolver(debug.SymbolResolver(elf_path, base, show_inline=not args.no_inlined_by))

def get_backtrace_formatter(args):
    if not args.backtrace:
        return lambda backtrace: ''

    return trace.BacktraceFormatter(symbol_resolver(args), src_addr_formatter(args))

def list_trace(args):
    def data_formatter(sample):
        if args.tcpdump and is_net_packet_sample(sample):
            return format_packet_sample(sample)
        return sample.format_data(sample)

    backtrace_formatter = get_backtrace_formatter(args)
    time_range = get_time_range(args)
    with get_trace_reader(args) as reader:
        for t in reader.get_traces():
            if t.time in time_range:
                print t.format(backtrace_formatter, data_formatter=data_formatter)

def mem_analys(args):
    mallocs = {}

    node_filters = []
    if args.min_count:
        node_filters.append(memory_analyzer.filter_min_count(args.min_count))
    if args.min_hits:
        if args.min_hits.endswith('%'):
            min_percent = parse_percentage(args.min_hits)
            node_filters.append(memory_analyzer.filter_min_bt_percentage(min_percent))
        else:
            min_count = int(args.min_hits)
            node_filters.append(memory_analyzer.filter_min_bt_count(min_count))

    with get_trace_reader(args) as reader:
        memory_analyzer.process_records(mallocs, reader.get_traces())
        memory_analyzer.show_results(mallocs,
            node_filters=node_filters,
            sorter=args.sort,
            group_by=args.group_by,
            show_backtrace=args.backtrace,
            symbol_resolver=symbol_resolver(args),
            src_addr_formatter=src_addr_formatter(args),
            max_levels=args.max_levels)

def add_time_slicing_options(parser):
    group = parser.add_argument_group('time slicing')
    group.add_argument("--since", action='store', help="show data starting on this timestamp [ns]")
    group.add_argument("--until", action='store', help="show data ending on this timestamp [ns] (exclusive)")
    group.add_argument("--period", action='store', help="""if only one of --since or --until is specified,
        the amount of time passed in this option will be used to calculate the other. The value is interpreted
        as nanoseconds unless unit is specified, eg: 500us""")

groupers = {
    'thread': prof.GroupByThread,
    'cpu': prof.GroupByCpu,
    'none': lambda: None,
}

def add_backtrace_options(parser):
    parser.add_argument("--min-hits", action='store',
        help="show only nodes with hit count not smaller than this. can be absolute number or a percentage, eg. 10%%")
    parser.add_argument("--max-levels", type=int, action='store', help="maximum number of tree levels to show")

def add_profile_options(parser):
    add_time_slicing_options(parser)
    group = parser.add_argument_group('profile options')
    group.add_argument("-r", "--caller-oriented", action='store_true', help="change orientation to caller-based; reverses order of frames")
    group.add_argument("-g", "--group-by", choices=groupers.keys(), default='none', help="group samples by given criteria")
    group.add_argument("--function", action='store', help="use given function as tree root")
    group.add_argument("--min-duration", action='store', help="show only nodes with resident time not shorter than this, eg: 200ms")
    add_backtrace_options(group)

class sample_name_is(object):
    def __init__(self, name):
        self.name = name

    def __call__(self, sample):
        return sample.name == self.name

def get_wait_profile(traces):
    return prof.get_duration_profile(traces, sample_name_is("sched_wait"))

def get_time_range(args):
    default_unit = 's'
    start = prof.parse_time_as_nanos(args.since, default_unit=default_unit) if args.since else None
    end = prof.parse_time_as_nanos(args.until, default_unit=default_unit) if args.until else None

    if args.period:
        if start and end:
            raise InvalidArgumentsException("--period cannot be used when both --since and --until are specified")

        period = prof.parse_time_as_nanos(args.period)
        if start:
            end = start + period
        elif end:
            start = end - period
        else:
            raise InvalidArgumentsException("--period must be used with --since or --until specified")

    return trace.TimeRange(start, end)

def parse_percentage(text):
    m = re.match(r'(?P<perc>[\d]+(\.[\d]*)?)%', text)
    if not m:
        raise InvalidArgumentsException('invalid format: ' + text)
    return float(m.group('perc'))

class MinHitPercentageFilter:
    def __init__(self, min_percentage):
        self.min_percentage = min_percentage

    def __call__(self, node, tree_root):
        return float(node.hit_count) / tree_root.hit_count * 100 >= self.min_percentage

class MinHitCountFilter:
    def __init__(self, min_hit_count):
        self.min_hit_count = min_hit_count

    def __call__(self, node, tree_root):
        return node.hit_count >= self.min_hit_count

def show_profile(args, sample_producer):
    resolver = symbol_resolver(args)
    time_range = get_time_range(args)

    node_filters = []

    if args.min_duration:
        min_duration = prof.parse_time_as_nanos(args.min_duration)
        node_filters.append(lambda node, tree_root: node.resident_time >= min_duration)

    if args.min_hits:
        if args.min_hits.endswith('%'):
            node_filters.append(MinHitPercentageFilter(parse_percentage(args.min_hits)))
        else:
            node_filters.append(MinHitCountFilter(int(args.min_hits)))

    def node_filter(*args):
        for filter in node_filters:
            if not filter(*args):
                return False
        return True

    with get_trace_reader(args) as reader:
        prof.print_profile(sample_producer(reader.get_traces()),
            symbol_resolver=resolver,
            caller_oriented=args.caller_oriented,
            grouping=groupers[args.group_by](),
            src_addr_formatter=src_addr_formatter(args),
            root_function=args.function,
            node_filter=node_filter,
            time_range=time_range,
            max_levels=args.max_levels)

def extract(args):
    if args.exe:
        elf_path = args.exe
    elif args.debug:
        elf_path = 'build/debug/loader.elf'
    else:
        elf_path = 'build/release/loader.elf'

    if os.path.isfile(elf_path):
        if os.path.exists(args.tracefile):
            os.remove(args.tracefile)
            assert(not os.path.exists(args.tracefile))
        cmdline = ['gdb', elf_path, '-batch']
        # enable adding OSv's python modules to gdb, see
        # http://sourceware.org/gdb/onlinedocs/gdb/Auto_002dloading-safe-path.html
        cmdline.extend(['-iex', 'set auto-load safe-path .'])
        if args.remote:
            cmdline.extend(['-ex', 'target remote ' + args.remote])
        else:
            cmdline.extend(['-ex', 'conn'])
        cmdline.extend(['-ex', 'osv trace save ' + args.tracefile])
        proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        _stdout, _ = proc.communicate()
        if proc.returncode or not os.path.exists(args.tracefile):
            print(_stdout)
            sys.exit(1)
    else:
        print("error: %s not found" % (elf_path))
        sys.exit(1)

def prof_wait(args):
    show_profile(args, get_wait_profile)

def prof_lock(args):
    def get_profile(traces):
        return prof.get_duration_profile(traces, sample_name_is("mutex_lock_wait"))
    show_profile(args, get_profile)

def prof_idle(args):
    show_profile(args, prof.get_idle_profile)

def needs_dpkt():
    global dpkt
    try:
        import dpkt
    except ImportError:
        raise Exception("""Cannot import dpkt. Please install 'python-dpkt' system package.""")

class Protocol:
    IP = 1
    IGMP = 2
    ROUTE = 3
    AARP = 4
    ATALK2 = 5
    ATALK1 = 6
    ARP = 7
    IPX = 8
    ETHER = 9
    IPV6 = 10
    NATM = 11
    EPAIR = 12

def write_sample_to_pcap(sample, pcap_writer):
    assert(is_net_packet_sample(sample))

    ts = sample.time / 1e9
    proto = int(sample.data[0])
    if proto == Protocol.ETHER:
        pcap_writer.writepkt(sample.data[1], ts=ts)
    else:
        eth_types = {
            Protocol.IP: dpkt.ethernet.ETH_TYPE_IP,
            Protocol.ROUTE: dpkt.ethernet.ETH_TYPE_REVARP,
            Protocol.AARP: dpkt.ethernet.ETH_TYPE_ARP,
            Protocol.ARP: dpkt.ethernet.ETH_TYPE_ARP,
            Protocol.IPX: dpkt.ethernet.ETH_TYPE_IPX,
            Protocol.IPV6: dpkt.ethernet.ETH_TYPE_IP6,
        }

        pkt = dpkt.ethernet.Ethernet()
        pkt.data = sample.data[1]
        pkt.type = eth_types[proto]
        pcap_writer.writepkt(pkt, ts=ts)

def format_packet_sample(sample):
    needs_dpkt()
    proc = subprocess.Popen(['tcpdump', '-nn', '-t', '-r', '-'], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pcap = dpkt.pcap.Writer(proc.stdin)
    write_sample_to_pcap(sample, pcap)
    pcap.close()
    assert(proc.stdout.readline() == "reading from file -, link-type EN10MB (Ethernet)\n")
    packet_line = proc.stdout.readline().rstrip()
    proc.wait()
    return packet_line

def is_net_packet_sample(sample):
    return sample.name.startswith('net_packet_')

def is_input_net_packet_sample(sample):
    return sample.name == "net_packet_in"

def is_output_net_packet_sample(sample):
    return sample.name == "net_packet_out"

def pcap_dump(args, target=None):
    needs_dpkt()

    if not target:
        target = sys.stdout

    pcap_file = dpkt.pcap.Writer(target)
    try:
        with get_trace_reader(args) as reader:
            for sample in reader.get_traces():
                if is_input_net_packet_sample(sample) or is_output_net_packet_sample(sample):
                    write_sample_to_pcap(sample, pcap_file)
    finally:
        pcap_file.close()

def tcpdump(args):
    proc = subprocess.Popen(['tcpdump', '-nn', '-r', '-'], stdin=subprocess.PIPE, stdout=sys.stdout,
        stderr=subprocess.STDOUT)
    try:
        pcap_dump(args, target=proc.stdin)
    except:
        proc.kill()
        raise
    proc.wait()

def get_trace_filter(args):
    if args.tracepoint:
        return sample_name_is(args.tracepoint)

def prof_hit(args):
    show_profile(args, lambda traces: prof.get_hit_profile(traces, get_trace_filter(args)))

def prof_timed(args):
    show_profile(args, lambda traces: prof.get_duration_profile(traces, get_trace_filter(args)))

def get_timed_traces_per_function(timed_traces):
    traces_per_function = defaultdict(list)
    for timed in timed_traces:
        traces_per_function[timed.trace.name].append(timed)
    return traces_per_function

def get_percentile(sorted_samples, fraction):
    return sorted_samples[int(math.ceil(float(len(sorted_samples) - 1) * fraction))]

def format_duration(nanos):
    return "%4.3f" % (float(nanos) / 1e6)

def print_summary(args, printer=sys.stdout.write):
    time_range = get_time_range(args)
    timed_producer = prof.timed_trace_producer()
    timed_samples = []

    count_per_tp = defaultdict(lambda: 0)
    count = 0
    min_time = None
    max_time = None

    class CpuTimeRange:
        def __init__(self):
            self.min = None
            self.max = None

        def update(self, time):
            if self.min is None or time < self.min:
                self.min = time
            if self.max is None or time > self.max:
                self.max = time

    cpu_time_ranges = defaultdict(CpuTimeRange)

    with get_trace_reader(args) as reader:
        for t in reader.get_traces():
            if t.time in time_range:
                cpu_time_ranges[t.cpu].update(t.time)
                count += 1
                count_per_tp[t.tp] += 1

                if not min_time:
                    min_time = t.time
                else:
                    min_time = min(min_time, t.time)

                max_time = max(max_time, t.time)

            if args.timed:
                timed = timed_producer(t)
                if timed:
                    timed_samples.append(timed)

    if args.timed:
        timed_samples.extend((timed_producer.finish()))

    if count == 0:
        print "No samples"
        return

    print "Collected %d samples spanning %s" % (count, prof.format_time(max_time - min_time))

    print "\nTime ranges:\n"
    for cpu, r in sorted(cpu_time_ranges.items(), key=lambda (c, r): r.min):
        print "  CPU 0x%02d: %s - %s = %10s" % (cpu,
            trace.format_time(r.min),
            trace.format_time(r.max),
            prof.format_time(r.max - r.min))

    max_name_len = reduce(max, map(lambda tp: len(tp.name), count_per_tp.iterkeys()))
    format = "  %%-%ds %%8s" % (max_name_len)
    print "\nTracepoint statistics:\n"
    print format % ("name", "count")
    print format % ("----", "-----")

    for tp, count in sorted(count_per_tp.iteritems(), key=lambda (tp, count): tp.name):
        print format % (tp.name, count)

    if args.timed:
        format = "  %-20s %8s %8s %8s %8s %8s %8s %8s %15s"
        print "\nTimed tracepoints [ms]:\n"

        timed_samples = filter(lambda t: t.time_range.intersection(time_range), timed_samples)

        if not timed_samples:
            print "  None"
        else:
            print format % ("name", "count", "min", "50%", "90%", "99%", "99.9%", "max", "total")
            print format % ("----", "-----", "---", "---", "---", "---", "-----", "---", "-----")

            for name, traces in get_timed_traces_per_function(timed_samples).iteritems():
                samples = sorted(list((t.time_range.intersection(time_range).length() for t in traces)))
                print format % (
                    name,
                    len(samples),
                    format_duration(get_percentile(samples, 0)),
                    format_duration(get_percentile(samples, 0.5)),
                    format_duration(get_percentile(samples, 0.9)),
                    format_duration(get_percentile(samples, 0.99)),
                    format_duration(get_percentile(samples, 0.999)),
                    format_duration(get_percentile(samples, 1)),
                    format_duration(sum(samples)))

    print

def list_cpu_load(args):
    load_per_cpu = {}
    max_cpu = 0
    n_defined = 0

    with get_trace_reader(args) as reader:
        for t in reader.get_traces():
            if t.name == "sched_load":
                if not t.cpu in load_per_cpu:
                    n_defined += 1

                load_per_cpu[t.cpu] = t.data[0]
                max_cpu = max(max_cpu, t.cpu)

                if args.cpus:
                    if n_defined < args.cpus:
                        continue

                    if max_cpu >= args.cpus:
                        raise Exception('Encountered CPU with id=%d but user claimed there are %d CPUs' % (max_cpu, args.cpus))

                if args.format == 'csv':
                    sys.stdout.write(trace.format_time(t.time))
                    for id in range(max_cpu + 1):
                        sys.stdout.write(',')
                        if id in load_per_cpu:
                            sys.stdout.write('%d' % load_per_cpu[id])
                else:
                    sys.stdout.write('%-20s' % trace.format_time(t.time))
                    for id in range(max_cpu + 1):
                        if id in load_per_cpu:
                            sys.stdout.write('%3d' % load_per_cpu[id])
                        else:
                            sys.stdout.write('%3s' % '')
                sys.stdout.write('\n')

def list_timed(args):
    bt_formatter = get_backtrace_formatter(args)
    time_range = get_time_range(args)

    with get_trace_reader(args) as reader:
        timed_traces = prof.get_timed_traces(reader.get_traces(), time_range)

        if args.sort:
            if args.sort == 'duration':
                order = -1
            elif args.sort == 'time':
                order = 1
            timed_traces = sorted(timed_traces, key=lambda timed: order * getattr(timed, args.sort))

        for timed in timed_traces:
            t = timed.trace
            print '0x%016x %-15s %2d %20s %7s %-20s %s%s' % (
                            t.thread.ptr,
                            t.thread.name,
                            t.cpu,
                            trace.format_time(t.time),
                            trace.format_duration(timed.duration),
                            t.name,
                            trace.Trace.format_data(t),
                            bt_formatter(t.backtrace))

def list_wakeup_latency(args):
    bt_formatter = get_backtrace_formatter(args)
    time_range = get_time_range(args)

    class WaitingThread:
        def __init__(self):
            self.wait = None
            self.wake = None

    waiting = defaultdict(WaitingThread)

    def format_wakeup_latency(nanos):
        return "%4.6f" % (float(nanos) / 1e6)

    if not args.no_header:
        print '%-18s %-15s %3s %20s %13s %9s %s' % (
            "THREAD", "THREAD-NAME", "CPU", "TIMESTAMP[s]", "WAKEUP[ms]", "WAIT[ms]", "BACKTRACE"
        )

    with get_trace_reader(args) as reader:
        for t in reader.get_traces():
            if t.name == "sched_wait":
                waiting[t.thread.ptr].wait = t
            elif t.name == "sched_wake":
                thread_id = t.data[0]
                if waiting[thread_id].wait:
                    waiting[thread_id].wake = t
            elif t.name == "sched_wait_ret":
                waiting_thread = waiting.pop(t.thread.ptr, None)
                if waiting_thread and waiting_thread.wake:
                    # See https://github.com/cloudius-systems/osv/issues/295
                    if t.cpu == waiting_thread.wait.cpu:
                        wakeup_delay = t.time - waiting_thread.wake.time
                        wait_time = t.time - waiting_thread.wait.time
                        print '0x%016x %-15s %3d %20s %13s %9s %s' % (
                                    t.thread.ptr,
                                    t.thread.name,
                                    t.cpu,
                                    trace.format_time(t.time),
                                    format_wakeup_latency(wakeup_delay),
                                    trace.format_duration(wait_time),
                                    bt_formatter(t.backtrace))

def add_trace_listing_options(parser):
    add_time_slicing_options(parser)
    add_trace_source_options(parser)
    add_symbol_resolution_options(parser)
    parser.add_argument("-b", "--backtrace", action="store_true", help="show backtrace")
    parser.add_argument("--no-header", action="store_true", help="do not show the header")

def convert_dump(args):
    if os.path.isfile(args.dumpfile):
        if os.path.exists(args.tracefile):
            os.remove(args.tracefile)
            assert(not os.path.exists(args.tracefile))
        print "Converting dump %s -> %s" % (args.dumpfile, args.tracefile)
        td = trace.TraceDumpReader(args.dumpfile)
        trace.write_to_file(args.tracefile, list(td.traces()))
    else:
        print("error: %s not found" % (args.dumpfile))
        sys.exit(1)

def download_dump(args):
    if os.path.exists(args.tracefile):
        os.remove(args.tracefile)
        assert(not os.path.exists(args.tracefile))

    file = args.tracefile
    client = Client(args)
    url = client.get_url() + "/trace/buffers"

    print "Downloading %s -> %s" % (url, file)

    r = requests.get(url, stream=True, **client.get_request_kwargs())
    size = int(r.headers['content-length'])
    current = 0

    with open(file, 'wb') as out_file:
        for chunk in r.iter_content(8192):
            out_file.write(chunk)
            current += len(chunk)
            sys.stdout.write("[{0:8d} / {1:8d} k] {3} {2:.2f}%\r".format(current/1024, size/1024, 100.0*current/size, ('='*32*(current/size)) + '>'))
            if current >= size:
                sys.stdout.write("\n")
            sys.stdout.flush()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="trace file processing")
    subparsers = parser.add_subparsers(help="Command")

    cmd_list = subparsers.add_parser("list", help="list trace")
    add_trace_listing_options(cmd_list)
    cmd_list.add_argument("--tcpdump", action="store_true")
    cmd_list.set_defaults(func=list_trace, paginate=True)

    cmd_wakeup_latency = subparsers.add_parser("wakeup-latency")
    add_trace_listing_options(cmd_wakeup_latency)
    cmd_wakeup_latency.set_defaults(func=list_wakeup_latency, paginate=True)

    cmd_load = subparsers.add_parser("cpu-load")
    cmd_load.add_argument('--format', '-f', action="store", choices=['csv', 'text'], default='text')
    cmd_load.add_argument('--cpus', '-c', action="store", type=int,
        help="shows data only when load is known on all CPUs. user must specify the number of CPUs.")
    add_trace_listing_options(cmd_load)
    cmd_load.set_defaults(func=list_cpu_load, paginate=True)

    cmd_list_timed = subparsers.add_parser("list-timed", help="list timed traces", description="""
        Prints block samples along with their duration in seconds with nanosecond precision. The duration
        is calculated bu subtracting timestamps between entry sample and the matched ending sample.
        The convention is that the ending sample has the same name as the entry sample plus '_ret' or '_err' suffix.
        Specifying a time range will result in only those samples being printed which overlap with the time range.
        """)
    add_trace_listing_options(cmd_list_timed)
    cmd_list_timed.add_argument("--sort", action="store", choices=['duration', 'time'], help="sort samples by given field")
    cmd_list_timed.set_defaults(func=list_timed, paginate=True)

    cmd_summary = subparsers.add_parser("summary", help="print trace summery", description="""
        Prints basic statistics about the trace.
        """)
    add_time_slicing_options(cmd_summary)
    add_trace_source_options(cmd_summary)
    cmd_summary.add_argument("--timed", action="store_true", help="print percentile table of timed trace samples")
    cmd_summary.set_defaults(func=print_summary)

    cmd_prof_wait = subparsers.add_parser("prof-wait", help="show wait profile", description="""
        Prints profile showing amount of time spent inside sched::thread::wait(). Among other
        things this includes time a thread was blocked on a mutex or condvar.
        Requires sched_wait and sched_wait_ret tracepoints.
        Requires trace samples with backtrace.
        """)
    add_symbol_resolution_options(cmd_prof_wait)
    add_trace_source_options(cmd_prof_wait)
    add_profile_options(cmd_prof_wait)
    cmd_prof_wait.set_defaults(func=prof_wait, paginate=True)

    cmd_prof_lock = subparsers.add_parser("prof-lock", help="show lock contention profile", description="""
        Prints profile showing amount of time for which threads were blocked witing on a mutex.
        This can be used to estimate lock contention.
        """)
    add_symbol_resolution_options(cmd_prof_lock)
    add_trace_source_options(cmd_prof_lock)
    add_profile_options(cmd_prof_lock)
    cmd_prof_lock.set_defaults(func=prof_lock, paginate=True)

    cmd_prof_idle = subparsers.add_parser("prof-idle")
    add_symbol_resolution_options(cmd_prof_idle)
    add_trace_source_options(cmd_prof_idle)
    add_profile_options(cmd_prof_idle)
    cmd_prof_idle.set_defaults(func=prof_idle, paginate=True)

    cmd_prof_hit = subparsers.add_parser("prof", help="show trace hit profile", description="""
        Prints profile showing number of times given tracepoint was reached.
        Requires trace samples with backtrace.
        """)
    add_symbol_resolution_options(cmd_prof_hit)
    add_trace_source_options(cmd_prof_hit)
    add_profile_options(cmd_prof_hit)
    cmd_prof_hit.add_argument("-t", "--tracepoint", action="store", help="name of the tracepoint to count")
    cmd_prof_hit.set_defaults(func=prof_hit, paginate=True)

    cmd_prof_timed = subparsers.add_parser("prof-timed", help="show duration profile of timed samples")
    add_symbol_resolution_options(cmd_prof_timed)
    add_trace_source_options(cmd_prof_timed)
    add_profile_options(cmd_prof_timed)
    cmd_prof_timed.add_argument("-t", "--tracepoint", action="store", required=True,
        help="name of the timed tracepoint to show; shows all by default")
    cmd_prof_timed.set_defaults(func=prof_timed, paginate=True)

    cmd_extract = subparsers.add_parser("extract", help="extract trace from running instance", description="""
        Extracts trace from a running OSv instance via GDB.
        """)
    add_symbol_resolution_options(cmd_extract)
    add_trace_source_options(cmd_extract)
    cmd_extract.add_argument("-r", "--remote", action="store", help="remote node address:port")
    cmd_extract.set_defaults(func=extract)

    cmd_pcap_dump = subparsers.add_parser("pcap-dump")
    add_trace_source_options(cmd_pcap_dump)
    cmd_pcap_dump.set_defaults(func=pcap_dump)

    cmd_tcpdump = subparsers.add_parser("tcpdump")
    add_trace_source_options(cmd_tcpdump)
    cmd_tcpdump.set_defaults(func=tcpdump, paginate=True)

    cmd_memory_analyzer = subparsers.add_parser("memory-analyzer",
        help="show memory allocation analysis", description="""
        Prints profile showing number of memory allocations, their size, alignment and allocator.
        Requires memory_* tracepoints enabled.
        """)
    add_trace_source_options(cmd_memory_analyzer)
    cmd_memory_analyzer.add_argument("--min-count", action='store', type=int,
        help="show only allocations at least as frequent as the specified threshold")
    cmd_memory_analyzer.add_argument("-s", "--sort",
        choices=memory_analyzer.sorters, default='size',
           help='sort allocations by given criteria')
    cmd_memory_analyzer.add_argument("-g", "--group-by",
        choices=memory_analyzer.groups, action='store',
        default=['allocator', 'alignment', 'allocated', 'requested'],
        nargs='*', help='groups allocations by given criteria')
    cmd_memory_analyzer.add_argument("--no-backtrace", action="store_false",
        default=True, dest='backtrace', help="never show backtrace")
    add_symbol_resolution_options(cmd_memory_analyzer)
    group = cmd_memory_analyzer.add_argument_group('backtrace options')
    add_backtrace_options(group)
    cmd_memory_analyzer.set_defaults(func=mem_analys, paginate=True)

    cmd_convert_dump = subparsers.add_parser("convert-dump", help="convert trace dump file (REST)"
                                             , description="""
                                             Converts trace dump acquired via REST Api to trace listing format
                                             """)
    add_trace_source_options(cmd_convert_dump)
    cmd_convert_dump.add_argument("-f", "--dumpfile", action="store",
                                  help="Trace dump file",
                                  default="buffers")
    cmd_convert_dump.set_defaults(func=convert_dump, paginate=False)

    cmd_download_dump = subparsers.add_parser("download", help="download trace dump file (REST)"
                                             , description="""
                                             Downloads a trace dump via REST Api
                                             """)
    add_trace_source_options(cmd_download_dump)
    Client.add_arguments(cmd_download_dump, use_full_url=True)
    cmd_download_dump.set_defaults(func=download_dump, paginate=False)


    args = parser.parse_args()

    if getattr(args, 'paginate', False):
        less_process = subprocess.Popen(['less', '-FX'], stdin=subprocess.PIPE)
        sys.stdout = less_process.stdin
    else:
        less_process = None

    try:
        args.func(args)
    except InvalidArgumentsException as e:
        print "Invalid arguments:", e.message
    except IOError as e:
        if e.errno != errno.EPIPE:
            raise
    finally:
        sys.stdout.close()
        if less_process:
            less_process.wait()
