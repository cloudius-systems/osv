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

def get_hit_profile(traces, filter=None):
    for trace in traces:
        if trace.backtrace and (not filter or filter(trace)):
            yield ProfSample(trace.time, trace.cpu, trace.thread, trace.backtrace)

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
            yield ProfSample(entry_trace.time, trace.cpu, trace.thread, entry_trace.backtrace, resident_time=duration)

    for thread, trace in entry_traces_per_thread.iteritems():
        duration = last_time - trace.time
        yield ProfSample(trace.time, trace.cpu, thread, trace.backtrace, resident_time=duration)

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
