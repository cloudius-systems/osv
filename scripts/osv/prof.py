import sys
from operator import attrgetter
from osv import trace, tree, debug
import itertools
import re

class ProfNode(tree.TreeNode):
    def __init__(self, key):
        super(ProfNode, self).__init__(key)
        self.hit_count = 0
        self.resident_time = 0
        self.tail = []

    def hit(self, resident_time=None):
        self.hit_count += 1
        self.resident_time += resident_time

    @property
    def attributes(self):
        return {
            'hit_count': self.hit_count,
            'resident_time': self.resident_time
        }

class ProfSample:
    def __init__(self, timestamp, cpu, thread, backtrace, resident_time=None):
        self.cpu = cpu
        self.thread = thread
        self.backtrace = backtrace
        self.timestamp = timestamp
        self.resident_time = resident_time or 0

    @property
    def time_range(self):
        return trace.TimeRange(self.timestamp, self.timestamp + self.resident_time)

    def intersection(self, time_range):
        intersection = self.time_range.intersection(time_range)
        if not intersection:
            return None
        return ProfSample(intersection.begin, self.cpu, self.thread, self.backtrace,
            resident_time=intersection.end - intersection.begin)

time_units = [
    (1e9 * 3600, "h"),
    (1e9 * 60, "m"),
    (1e9, "s"),
    (1e6, "ms"),
    (1e3, "us"),
    (1, "ns")
]

def parse_time_as_nanos(text):
    for level, name in sorted(time_units, key=lambda (level, name): -len(name)):
        if text.endswith(name):
            return float(text.rstrip(name)) * level
    return float(text)

def format_time(time, format="%.2f %s"):
    for level, name in sorted(time_units, key=lambda (level, name): -level):
        if time >= level:
            return format % (float(time) / level, name)
    return str(time)

unimportant_functions = set([
    'operator()',
    'std::function<void ()>::operator()() const',
    '_M_invoke',
    ])

unimportant_prefixes = [
    ('tracepoint_base::log_backtrace(trace_record*, unsigned char*&)',
     'log',
     'trace_slow_path',
     'operator()',
     'prof::cpu_sampler::timer_fired()',
     'sched::timer_base::expire()',
     'sched::timer_list::fired()',
     'interrupt_descriptor_table::invoke_interrupt(unsigned int)',
     'interrupt',
     'interrupt_entry_common'),

    ('tracepoint_base::log_backtrace(trace_record*, unsigned char*&)',
     'log',
     'trace_slow_path'),
]

bottom_of_stack = set(['thread_main', 'thread_main_c'])

def strip_garbage(backtrace):
    def is_good(src_addr):
        if not src_addr.name:
            return True
        return not src_addr.name in unimportant_functions

    for chain in unimportant_prefixes:
        if len(backtrace) >= len(chain) and \
                tuple(map(attrgetter('name'), backtrace[:len(chain)])) == chain:
            backtrace = backtrace[len(chain):]
            break

    backtrace = list(filter(is_good, backtrace))

    for i, src_addr in enumerate(backtrace):
        if src_addr.name in bottom_of_stack:
            return backtrace[:i]

    return backtrace

def get_hit_profile(traces, filter=None):
    for trace in traces:
        if trace.backtrace and (not filter or filter(trace)):
            yield ProfSample(trace.time, trace.cpu, trace.thread, trace.backtrace)


class TimedTraceMatcher(object):
    def is_entry_or_exit(self, sample):
        """
        True => entry, False => exit, None => neither
        """
        pass

    def get_correlation_id(self, sample):
        """
        Returns an object which identifies the pair within the set
        of pairs maintained by this matcher
        """
        pass

class PairTimedTraceMatcher(TimedTraceMatcher):
    def __init__(self, entry_trace_name, exit_trace_name):
        self.entry_trace_name = entry_trace_name
        self.exit_trace_name = exit_trace_name

    def is_entry_or_exit(self, sample):
        if sample.name == self.entry_trace_name:
            return True
        if sample.name == self.exit_trace_name:
            return False

    def get_correlation_id(self, sample):
        return sample.thread

class TimedConventionMatcher(TimedTraceMatcher):
    def __init__(self):
        self.block_tracepoints = set()

    def get_name_of_ended_func(self, name):
        m = re.match('(?P<func>.*)(_ret|_err)', name)
        if m:
            return m.group('func')

    def is_entry_or_exit(self, sample):
        ended = self.get_name_of_ended_func(sample.name)
        if ended:
            self.block_tracepoints.add(ended)
            return False

        if sample.name in self.block_tracepoints:
            return True

    def get_correlation_id(self, sample):
        ended = self.get_name_of_ended_func(sample.name)
        if ended:
            return (sample.thread, ended)
        return (sample.thread, sample.name)

class timed_trace_producer(object):
    pair_matchers = [
        PairTimedTraceMatcher('mutex_lock_wait', 'mutex_lock_wake')
    ]

    def __init__(self):
        self.matcher_by_name = dict()
        for m in self.pair_matchers:
            self.matcher_by_name[m.entry_trace_name] = m
            self.matcher_by_name[m.exit_trace_name] = m

        self.convention_matcher = TimedConventionMatcher()
        self.open_samples = {}
        self.earliest_trace_per_cpu = {}
        self.last_time = None

    def __call__(self, sample):
        if not sample.cpu in self.earliest_trace_per_cpu:
            self.earliest_trace_per_cpu[sample.cpu] = sample

        self.last_time = max(self.last_time, sample.time)

        matcher = self.matcher_by_name.get(sample.name, None)
        if not matcher:
            matcher = self.convention_matcher

        is_entry = matcher.is_entry_or_exit(sample)
        if is_entry == None:
            return

        id = (matcher, matcher.get_correlation_id(sample))
        if is_entry:
            if id in self.open_samples:
                old = self.open_samples[id]
                if self.earliest_trace_per_cpu[sample.cpu] > old:
                    pass
                else:
                    raise Exception('Nested entry:\n%s\n%s\n' % (str(old), str(sample)))
            self.open_samples[id] = sample
        else:
            entry_trace = self.open_samples.pop(id, None)
            if not entry_trace:
                return
            if entry_trace.cpu != sample.cpu and self.earliest_trace_per_cpu[sample.cpu] > entry_trace:
                return
            duration = sample.time - entry_trace.time
            return trace.TimedTrace(entry_trace, duration)

    def finish(self):
        for sample in self.open_samples.itervalues():
            duration = self.last_time - sample.time
            yield trace.TimedTrace(sample, duration)

    def get_all(self, traces):
        for t in traces:
            timed = self(t)
            if timed:
                yield timed
        for timed in self.finish():
            yield timed

def get_timed_traces(traces, time_range=None):
    producer = timed_trace_producer()
    for timed in producer.get_all(traces):
        if not time_range or timed.time_range.intersection(time_range):
            yield timed

def get_duration_profile(traces, filter=None):
    for timed in get_timed_traces(traces):
        t = timed.trace
        if (not filter or filter(t)) and t.backtrace:
            yield ProfSample(t.time, t.cpu, t.thread, t.backtrace, resident_time=timed.duration)

def collapse_similar(node):
    while node.has_only_one_child():
        child = next(node.children)
        if node.attributes == child.attributes:
            node.squash_child()
            node.tail.append(child.key)
        else:
            break

    for child in node.children:
        collapse_similar(child)

def strip_level(node, level):
    if level <= 0:
        node.remove_all()
    else:
        for child in node.children:
            strip_level(child, level - 1)

def find_frame_index(frames, name):
    for i, src_addr in enumerate(frames):
        if src_addr.name and src_addr.name == name:
            return i
    return None

class GroupByThread:
    def get_group(self, sample):
        return sample.thread

    def format(self, group):
        return 'Thread 0x%x' % group

class GroupByCpu:
    def get_group(self, sample):
        return sample.cpu

    def format(self, group):
        return 'CPU 0x%02x' % group

def default_printer(args):
    sys.stdout.write(args)

def print_profile(samples, symbol_resolver, caller_oriented=False,
        printer=default_printer, time_range=None, src_addr_formatter=debug.SourceAddress.__str__,
        node_filter=None, order=None, root_function=None, max_levels=None, grouping=None):
    groups = {}

    for sample in samples:
        if time_range:
            sample = sample.intersection(time_range)
            if not sample:
                continue

        frames = list(debug.resolve_all(symbol_resolver, (addr - 1 for addr in sample.backtrace)))
        frames = strip_garbage(frames)
        if caller_oriented:
            frames.reverse()

        if root_function:
            i = find_frame_index(frames, root_function)
            if i:
                frames = frames[i:]
            else:
                frames = None

        if frames:
            key = grouping.get_group(sample) if grouping else None
            node = groups.get(key, None)
            if not node:
                node = ProfNode('All')
                groups[key] = node

            node.hit(sample.resident_time)
            for src_addr in frames:
                node = node.get_or_add(src_addr_formatter(src_addr))
                node.hit(sample.resident_time)

    def format_node(node, root):
        attributes = ''
        percentage_subject_getter = attrgetter('hit_count')

        if root.resident_time:
            attributes += format_time(node.resident_time) + ' '
            percentage_subject_getter = attrgetter('resident_time')

        bracket_attributes = []
        if percentage_subject_getter(root):
            percentage = float(percentage_subject_getter(node)) * 100 / percentage_subject_getter(root)
            bracket_attributes.append('%.2f%%' % percentage)

        bracket_attributes.append('#%d' % node.hit_count)

        label = '\n '.join([node.key] + node.tail)
        return "%s(%s) %s" % (attributes, ', '.join(bracket_attributes), label)

    if not order:
        order = lambda node: (-node.resident_time, -node.hit_count)

    for group, tree_root in sorted(groups.iteritems(), key=lambda (thread, node): order(node)):
        collapse_similar(tree_root)

        if max_levels:
            strip_level(tree_root, max_levels)

        if grouping:
            printer('\n=== ' + grouping.format(group) + ' ===\n\n')

        tree.print_tree(tree_root,
                formatter=lambda node: format_node(node, tree_root),
                order_by=order,
                printer=printer,
                node_filter=lambda node: node_filter(node, tree_root))
