#!/usr/bin/env python
import sys
import errno
import argparse
import re
import os
import math
import subprocess
from itertools import ifilter
from collections import defaultdict
from operator import attrgetter

from osv import trace, debug, prof

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
    group.add_argument("-L", "--show-line-number", action='store_true', help="show line numbers")
    group.add_argument("-A", "--show-address", action='store_true', help="show raw addresses")
    group.add_argument("-F", "--show-file-name", action='store_true', help="show file names")

class BeautifyingResolver(object):
    def __init__(self, delegate):
        self.delegate = delegate

    def __call__(self, addr):
        src_addr = self.delegate(addr)
        if src_addr.name:
            if src_addr.name.startswith("void sched::thread::do_wait_until<"):
                src_addr.name = "sched::thread::do_wait_until"
            elif src_addr.name.startswith("do_wait_until<"):
                src_addr.name = "do_wait_until"
        return src_addr

def symbol_resolver(args):
    if args.no_resolve:
        return debug.DummyResolver()

    if args.exe:
        elf_path = args.exe
    elif args.debug:
        elf_path = 'build/debug/loader.elf'
    else:
        elf_path = 'build/release/loader.elf'

    return BeautifyingResolver(debug.SymbolResolver(elf_path))

def get_backtrace_formatter(args):
    if not args.backtrace:
        return lambda backtrace: ''

    return trace.BacktraceFormatter(
        symbol_printer(symbol_resolver(args), src_addr_formatter(args)))

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

def add_profile_options(parser):
    add_time_slicing_options(parser)
    group = parser.add_argument_group('profile options')
    group.add_argument("-r", "--caller-oriented", action='store_true', help="change orientation to caller-based; reverses order of frames")
    group.add_argument("-g", "--group-by", choices=groupers.keys(), default='none', help="group samples by given criteria")
    group.add_argument("--function", action='store', help="use given function as tree root")
    group.add_argument("--min-duration", action='store', help="show only nodes with resident time not shorter than this, eg: 200ms")
    group.add_argument("--min-hits", action='store',
        help="show only nodes with hit count not smaller than this. can be absolute number or a percentage, eg. 10%%")
    group.add_argument("--max-levels", type=int, action='store', help="maximum number of tree levels to show")

def get_wait_profile(traces):
    return prof.get_duration_profile(traces, "sched_wait", "sched_wait_ret")

def get_time_range(args):
    start = prof.parse_time_as_nanos(args.since) if args.since else None
    end = prof.parse_time_as_nanos(args.until) if args.until else None

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

    if (os.path.isfile(elf_path)):
       sys.exit(os.system("gdb %s -batch -ex conn -ex \"osv trace save %s\"" % (elf_path, args.tracefile)))
    else:
        print("error: %s not found" % (elf_path))
        sys.exit(1)

def prof_wait(args):
    show_profile(args, get_wait_profile)


def needs_dpkt():
    global dpkt
    try:
        import dpkt
    except ImportError:
        raise Exception("""Cannot import dpkt. If you don't have it installed you can get it from
             https://code.google.com/p/dpkt/downloads""")

def write_sample_to_pcap(sample, pcap_writer):
    ts = sample.time / 1e9
    if sample.name == "net_packet_eth":
        pcap_writer.writepkt(str(sample.data[1]), ts=ts)
    elif sample.name == "net_packet_loopback":
        pkt = dpkt.ethernet.Ethernet()
        pkt.data = sample.data[0]
        pcap_writer.writepkt(pkt, ts=ts)
    else:
        raise Exception('Unsupported tracepoint: ' + sample.name)

def format_packet_sample(sample):
    assert(is_net_packet_sample(sample))
    needs_dpkt()
    proc = subprocess.Popen(['tcpdump', '-tn', '-r', '-'], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pcap = dpkt.pcap.Writer(proc.stdin)
    write_sample_to_pcap(sample, pcap)
    pcap.close()
    assert(proc.stdout.readline() == "reading from file -, link-type EN10MB (Ethernet)\n")
    packet_line = proc.stdout.readline().rstrip()
    proc.wait()
    return packet_line

def is_net_packet_sample(sample):
    return sample.name in ["net_packet_eth", "net_packet_loopback"]

def pcap_dump(args, target=None):
    needs_dpkt()

    if not target:
        target = sys.stdout

    pcap_file = dpkt.pcap.Writer(target)
    try:
        with get_trace_reader(args) as reader:
            for sample in reader.get_traces():
                if is_net_packet_sample(sample):
                    write_sample_to_pcap(sample, pcap_file)
    finally:
        pcap_file.close()

def tcpdump(args):
    proc = subprocess.Popen(['tcpdump', '-n', '-r', '-'], stdin=subprocess.PIPE, stdout=sys.stdout,
        stderr=subprocess.STDOUT)
    try:
        pcap_dump(args, target=proc.stdin)
    except:
        proc.kill()
        raise
    proc.wait()

def prof_hit(args):
    if args.tracepoint:
        filter = lambda trace: trace.name == args.tracepoint
    else:
        filter = None
    show_profile(args, lambda traces: prof.get_hit_profile(traces, filter))

def get_name_of_ended_func(name):
        m = re.match('(?P<func>.*)(_ret|_err)', name)
        if m:
            return m.group('func')

class block_tracepoint_collector(object):
    def __init__(self):
        self.block_tracepoints = set()

    def __call__(self, tp):
        ended = get_name_of_ended_func(tp.name)
        if ended:
            self.block_tracepoints.add(ended)

    def __contains__(self, tp):
        return tp.name in self.block_tracepoints

class timed_trace_producer(object):
    def __init__(self):
        self.block_tracepoints = block_tracepoint_collector()
        self.open_functions = defaultdict(dict)

    def __call__(self, t):
        self.block_tracepoints(t.tp)

        name = t.name
        ended = get_name_of_ended_func(name)
        if ended:
            if ended in self.open_functions[t.thread]:
                timed = self.open_functions[t.thread].pop(ended)
                timed.duration = t.time - timed.trace.time
                return timed
        elif t.tp in self.block_tracepoints:
            if name in self.open_functions[t.thread]:
                raise Exception("Nested traces not supported: " + name)
            self.open_functions[t.thread][name] = trace.TimedTrace(t)

def get_timed_traces(traces, time_range):
    producer = timed_trace_producer()
    for t in traces:
        timed = producer(t)
        if timed and timed.time_range.intersection(time_range):
                yield timed

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
    timed_producer = timed_trace_producer()
    timed_samples = []

    count_per_tp = defaultdict(lambda: 0)
    count = 0
    min_time = None
    max_time = None

    with get_trace_reader(args) as reader:
        for t in reader.get_traces():
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

    if count == 0:
        print "No samples"
        return

    print "Collected %d samples spanning %s" % (count, prof.format_time(max_time - min_time))

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

        if not timed_samples:
            print "  None"
        else:
            print format % ("name", "count", "min", "50%", "90%", "99%", "99.9%", "max", "total")
            print format % ("----", "-----", "---", "---", "---", "---", "-----", "---", "-----")

            for name, traces in get_timed_traces_per_function(timed_samples).iteritems():
                    samples = sorted(map(attrgetter('duration'), traces))
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

def list_timed(args):
    bt_formatter = get_backtrace_formatter(args)
    time_range = get_time_range(args)

    with get_trace_reader(args) as reader:
        timed_traces = get_timed_traces(reader.get_traces(), time_range)

        if args.sort:
            if args.sort == 'duration':
                order = -1
            elif args.sort == 'time':
                order = 1
            timed_traces = sorted(timed_traces, key=lambda timed: order * getattr(timed, args.sort))

        for timed in timed_traces:
            t = timed.trace
            print '0x%016x %2d %20s %7s %-20s %s%s' % (
                            t.thread,
                            t.cpu,
                            trace.format_time(t.time),
                            trace.format_duration(timed.duration),
                            t.name,
                            t.format_data(),
                            bt_formatter(t.backtrace))

def add_trace_listing_options(parser):
    add_time_slicing_options(parser)
    add_trace_source_options(parser)
    add_symbol_resolution_options(parser)
    parser.add_argument("-b", "--backtrace", action="store_true", help="show backtrace")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="trace file processing")
    subparsers = parser.add_subparsers(help="Command")

    cmd_list = subparsers.add_parser("list", help="list trace")
    add_trace_listing_options(cmd_list)
    cmd_list.add_argument("--tcpdump", action="store_true")
    cmd_list.set_defaults(func=list_trace, paginate=True)

    cmd_list_timed = subparsers.add_parser("list-timed", help="list timed traces", description="""
        Prints block samples along with their duration in seconds with nanosecond precision. The duration
        is calculated bu subtracting timestamps between entry sample and the matched ending sample.
        The convention is that the ending sample has the same name as the entry sample plus '_ret' or '_err' suffix.
        Specifying a time range will result in only those samples being printed which overlap with the time range.
        """)
    add_trace_listing_options(cmd_list_timed)
    cmd_list_timed.add_argument("--sort", action="store", choices=['duration','time'], help="sort samples by given field")
    cmd_list_timed.set_defaults(func=list_timed, paginate=True)

    cmd_summary = subparsers.add_parser("summary", help="print trace summery", description="""
        Prints basic statistics about the trace.
        """)
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

    cmd_prof_hit = subparsers.add_parser("prof", help="show trace hit profile", description="""
        Prints profile showing number of times given tracepoint was reached.
        Requires trace samples with backtrace.
        """)
    add_symbol_resolution_options(cmd_prof_hit)
    add_trace_source_options(cmd_prof_hit)
    add_profile_options(cmd_prof_hit)
    cmd_prof_hit.add_argument("-t", "--tracepoint", action="store", help="name of the tracepoint to count")
    cmd_prof_hit.set_defaults(func=prof_hit, paginate=True)

    cmd_extract = subparsers.add_parser("extract", help="extract trace from running instance", description="""
        Extracts trace from a running OSv instance via GDB.
        """)
    add_symbol_resolution_options(cmd_extract)
    add_trace_source_options(cmd_extract)
    cmd_extract.set_defaults(func=extract)

    cmd_pcap_dump = subparsers.add_parser("pcap-dump")
    add_trace_source_options(cmd_pcap_dump)
    cmd_pcap_dump.set_defaults(func=pcap_dump)

    cmd_tcpdump = subparsers.add_parser("tcpdump")
    add_trace_source_options(cmd_tcpdump)
    cmd_tcpdump.set_defaults(func=tcpdump, paginate=True)

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
