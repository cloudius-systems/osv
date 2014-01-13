import sys
from operator import attrgetter
from osv import trace, tree, debug

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

class ProfSample:
    def __init__(self, timestamp, thread, backtrace, resident_time=None):
        self.thread = thread
        self.backtrace = backtrace
        self.timestamp = timestamp
        self.resident_time = resident_time or 0

    @property
    def time_range(self):
        return TimeRange(self.timestamp, self.timestamp + self.resident_time)

    def intersection(self, time_range):
        intersection = self.time_range.intersection(time_range)
        if not intersection:
            return None
        return ProfSample(intersection.begin, self.thread, self.backtrace,
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
    'trace_slow_path',
    'operator()',
    'std::function<void ()>::operator()() const',
    'tracepoint_base::do_log_backtrace',
    'tracepoint_base::log_backtrace(trace_record*, unsigned char*&)',
    'tracepoint_base::do_log_backtrace(trace_record*, unsigned char*&)'
    ])

bottom_of_stack = set(['thread_main', 'thread_main_c'])

def strip_garbage(backtrace):
    def is_good(src_addr):
        if not src_addr.name:
            return True
        return not src_addr.name in unimportant_functions

    backtrace = list(filter(is_good, backtrace))

    for i, src_addr in enumerate(backtrace):
        if src_addr.name in bottom_of_stack:
            return backtrace[:i]

    return backtrace

def get_duration_profile(traces, entry_trace_name, exit_trace_name):
    entry_traces_per_thread = {}
    last_time = None

    for trace in traces:
        last_time = max(last_time, trace.time)

        if not trace.backtrace:
            continue

        if trace.name == entry_trace_name:
            if trace.thread in entry_traces_per_thread:
                old = entry_traces_per_thread[trace.thread]
                raise Exception('Double entry:\n%s\n%s\n' % (str(old), str(trace)))
            entry_traces_per_thread[trace.thread] = trace

        elif trace.name == exit_trace_name:
            entry_trace = entry_traces_per_thread.pop(trace.thread, None)
            if not entry_trace:
                continue

            duration = trace.time - entry_trace.time
            yield ProfSample(entry_trace.time, trace.thread, entry_trace.backtrace, resident_time=duration)

    for thread, trace in entry_traces_per_thread.iteritems():
        duration = last_time - trace.time
        yield ProfSample(trace.time, thread, trace.backtrace, resident_time=duration)

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

def print_profile(samples, symbol_resolver, caller_oriented=False, merge_threads=True,
        printer=sys.stdout.write, time_range=None, src_addr_formatter=debug.SourceAddress.__str__,
        node_filter=None, order=None, root_function=None, max_levels=None):
    thread_profiles = {}

    if merge_threads:
        root = ProfNode('All')
        thread_profiles['All'] = root

    for sample in samples:
        if time_range:
            sample = sample.intersection(time_range)
            if not sample:
                continue

        frames = [symbol_resolver(addr - 1) for addr in sample.backtrace]
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
            if merge_threads:
                node = root
            else:
                node = thread_profiles.get(sample.thread, None)
                if not node:
                    node = ProfNode('All')
                    thread_profiles[sample.thread] = node

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

    for thread, tree_root in sorted(thread_profiles.iteritems(), key=lambda (thread, node): order(node)):
        collapse_similar(tree_root)

        if max_levels:
            strip_level(tree_root, max_levels)

        if not merge_threads:
            printer("\n=== Thread 0x%x ===\n\n" % thread)

        tree.print_tree(tree_root,
                formatter=lambda node: format_node(node, tree_root),
                order_by=order,
                printer=printer,
                node_filter=node_filter)
