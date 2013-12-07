#!/usr/bin/python2

import gdb
import re
import os, os.path
import struct
import json
import math
import itertools
import operator
from glob import glob
from collections import defaultdict

build_dir = os.path.dirname(gdb.current_objfile().filename)
external = build_dir + '/../../external'

virtio_driver_type = gdb.lookup_type('virtio::virtio_driver')

class status_enum_class(object):
    pass
status_enum = status_enum_class()

phys_mem = 0xffffc00000000000

def pt_index(addr, level):
    return (addr >> (12 + 9 * level)) & 511

def phys_cast(addr, type):
    return gdb.Value(addr + phys_mem).cast(type.pointer())

def read_vector(v):
    impl = v['_M_impl']
    ptr = impl['_M_start']
    end = impl['_M_finish']
    while ptr != end:
        yield ptr.dereference()
        ptr += 1

def load_elf(path, base):
    args = ''
    text_addr = '?'
    unwanted_sections = ['.text',
                         '.note.stapsdt',
                         '.gnu_debuglink',
                         '.gnu_debugdata',
                         '.shstrtab',
                         ]
    for line in os.popen('readelf -WS ' + path):
        m = re.match(r'\s*\[ *\d+\]\s+([\.\w\d_]+)\s+\w+\s+([0-9a-f]+).*', line)
        if m:
            section = m.group(1)
            if section == 'NULL':
                continue
            addr = hex(int(m.group(2), 16) + base)
            if section == '.text':
                text_addr = addr
            if section not in unwanted_sections:
                args += ' -s %s %s' % (section, addr)

    gdb.execute('add-symbol-file %s %s %s' % (path, text_addr, args))

class syminfo(object):
    cache = dict()
    def __init__(self, addr):
        if addr in syminfo.cache:
            cobj = syminfo.cache[addr]
            self.func = cobj.func
            self.source = cobj.source
            return
        infosym = gdb.execute('info symbol 0x%x' % addr, False, True)
        self.func = infosym[:infosym.find(" + ")]
        sal = gdb.find_pc_line(addr)
        try :
            # prefer (filename:line),
            self.source = '%s:%s' % (sal.symtab.filename, sal.line)
        except :
            # but if can't get it, at least give the name of the object
            if infosym.startswith("No symbol matches") :
                self.source = None
            else:
                self.source = infosym[infosym.rfind("/")+1:].rstrip()
        if self.source and self.source.startswith('../../'):
            self.source = self.source[6:]
        syminfo.cache[addr] = self
    def __str__(self):
        ret = self.func
        if self.source:
            ret += ' (%s)' % (self.source,)
        return ret
    @classmethod
    def clear_cache(clazz):
        clazz.cache.clear()

def translate(path):
    '''given a path, try to find it on the host OS'''
    name = os.path.basename(path)
    for top in [build_dir, external, '/zfs']:
        for root, dirs, files in os.walk(top):
            if name in files:
                return os.path.join(root, name)
    return None

class Connect(gdb.Command):
    '''Connect to a local kvm instance at port :1234'''
    def __init__(self):
        gdb.Command.__init__(self,
                             'connect',
                             gdb.COMMAND_NONE,
                             gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        gdb.execute('target remote :1234')
        global status_enum
        status_enum.running = gdb.parse_and_eval('sched::thread::running')
        status_enum.waiting = gdb.parse_and_eval('sched::thread::waiting')
        status_enum.queued = gdb.parse_and_eval('sched::thread::queued')
        status_enum.waking = gdb.parse_and_eval('sched::thread::waking')
        

Connect()


class LogTrace(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self,
                             'logtrace',
                             gdb.COMMAND_NONE,
                             gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        gdb.execute('shell rm -f gdb.txt')
        gdb.execute('set pagination off')
        gdb.execute('set logging on')
        gdb.execute('osv trace')
        gdb.execute('set logging off')

LogTrace()

#
# free_page_ranges generator, use pattern:
# for range in free_page_ranges():
#     pass
#
def free_page_ranges(node = None):
    if (node == None):
        fpr = gdb.lookup_global_symbol('memory::free_page_ranges').value()
        p = fpr['tree_']['data_']['node_plus_pred_']
        node = p['header_plus_size_']['header_']['parent_']
    
    if (long(node) != 0):
        page_range = node.cast(gdb.lookup_type('void').pointer()) - 8
        page_range = page_range.cast(gdb.lookup_type('memory::page_range').pointer())
        
        for x in free_page_ranges(node['left_']):
            yield x
            
        yield page_range
        
        for x in free_page_ranges(node['right_']):
            yield x

def vma_list(node = None):
    if (node == None):
        fpr = gdb.lookup_global_symbol('mmu::vma_list').value()
        p = fpr['tree_']['data_']['node_plus_pred_']
        node = p['header_plus_size_']['header_']['parent_']

    if (long(node) != 0):
        offset = gdb.parse_and_eval('(int)&((mmu::vma*)0)->_vma_list_hook');
        vma = node.cast(gdb.lookup_type('void').pointer()) - offset
        vma = vma.cast(gdb.lookup_type('mmu::vma').pointer())

        for x in vma_list(node['left_']):
            yield x

        yield vma

        for x in vma_list(node['right_']):
            yield x

class osv(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv',
                             gdb.COMMAND_USER, gdb.COMPLETE_COMMAND, True)

class osv_heap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv heap',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        for page_range in free_page_ranges():
            print '%s 0x%016x' % (page_range, page_range['size'])

class osv_memory(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv memory',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        freemem = 0
        for page_range in free_page_ranges():
            freemem += int(page_range['size'])

        mmapmem = 0
        for vma in vma_list():
            start = ulong(vma['_start'])
            end   = ulong(vma['_end'])
            size  = ulong(end - start)
            mmapmem += size
            
        memsize = gdb.parse_and_eval('memory::phys_mem_size')
        
        print ("Total Memory: %d Bytes" % memsize)
        print ("Mmap Memory:  %d Bytes (%.2f%%)" %
               (mmapmem, (mmapmem*100.0/memsize)))
        print ("Free Memory:  %d Bytes (%.2f%%)" % 
               (freemem, (freemem*100.0/memsize)))

class osv_mmap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv mmap',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        for vma in vma_list():
            start = ulong(vma['_start'])
            end   = ulong(vma['_end'])
            size  = ulong(end - start)
            print '0x%016x 0x%016x [%s kB]' % (start, end, size / 1024)
    
ulong_type = gdb.lookup_type('unsigned long')
timer_type = gdb.lookup_type('sched::timer_base')
thread_type = gdb.lookup_type('sched::thread')

active_thread_context = None

def ulong(x):
    if isinstance(x, gdb.Value):
        x = x.cast(ulong_type)
    x = long(x)
    if x < 0:
        x += 1L << 64
    return x

class osv_syms(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv syms',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        syminfo.clear_cache()
        p = gdb.lookup_global_symbol('elf::program::s_objs').value()
        p = p.dereference().address
        while long(p.dereference()):
            obj = p.dereference().dereference()
            base = long(obj['_base'])
            path = obj['_pathname']['_M_dataplus']['_M_p'].string()
            path = translate(path)
            print path, hex(base)
            load_elf(path, base)
            p += 1

class osv_info(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class cpu(object):
    def __init__(self, cpu_thread):
        self.load(cpu_thread)
    def load(self, cpu_thread):
        self.cpu_thread = cpu_thread
        self.id = cpu_thread.num - 1
        cur = gdb.selected_thread()
        try:
            self.cpu_thread.switch()
            old = gdb.selected_frame()
            try:
                gdb.newest_frame().select()
                self.rsp = ulong(gdb.parse_and_eval('$rsp'))
                self.rbp = ulong(gdb.parse_and_eval('$rbp'))
                self.rip = ulong(gdb.parse_and_eval('$rip'))
            finally:
                old.select()
        finally:
            cur.switch()
        g_cpus = gdb.parse_and_eval('sched::cpus._M_impl._M_start')
        self.obj = g_cpus + self.id

def template_arguments(gdb_type):
    n = 0;
    while True:
        try:
            yield gdb_type.template_argument(n)
            n += 1
        except RuntimeError:
            return

def get_template_arg_with_prefix(gdb_type, prefix):
    for arg in template_arguments(gdb_type):
        if str(arg).startswith(prefix):
            return arg

def get_base_class_offset(gdb_type, base_class_name):
    name_pattern = re.escape(base_class_name) + "(<.*>)?$"
    for field in gdb_type.fields():
        if field.is_base_class and re.match(name_pattern, field.name):
            return field.bitpos / 8

def derived_from(type, base_class):
    return len([x for x in type.fields()
                if x.is_base_class and x.type == base_class]) != 0

class unordered_map:

    def __init__(self, map_ref):
        map_header = map_ref['_M_h']
        map_type = map_header.type.strip_typedefs()
        self.node_type = gdb.lookup_type(str(map_type) +  '::__node_type').pointer()
        self.begin = map_header['_M_bbegin']

    def __iter__(self):
        begin = self.begin
        while begin:
            node = begin.cast(self.node_type).dereference()
            elem = node["_M_v"]
            yield elem["second"]
            begin = node["_M_nxt"]

class intrusive_list:
    size_t = gdb.lookup_type('size_t')

    def __init__(self, list_ref):
        list_type = list_ref.type.strip_typedefs()
        self.node_type = list_type.template_argument(0)
        self.root = list_ref['data_']['root_plus_size_']['root_']

        member_hook = get_template_arg_with_prefix(list_type, "boost::intrusive::member_hook")
        if member_hook:
            self.link_offset = member_hook.template_argument(2).cast(self.size_t)
        else:
            self.link_offset = get_base_class_offset(self.node_type, "boost::intrusive::list_base_hook")
            if self.link_offset == None:
                raise Exception("Class does not extend list_base_hook: " + str(self.node_type))

    def __iter__(self):
        hook = self.root['next_']
        while hook != self.root.address:
            node_ptr = hook.cast(self.size_t) - self.link_offset
            yield node_ptr.cast(self.node_type.pointer()).dereference()
            hook = hook['next_']

    def __nonzero__(self):
        return self.root['next_'] != self.root.address

class vmstate(object):
    def __init__(self):
        self.reload()

    def reload(self):
        self.load_cpu_list()
        self.load_thread_list()

    def load_cpu_list(self):
        # cause gdb to initialize thread list
        gdb.execute('info threads', False, True)
        cpu_list = {}
        for cpu_thread in gdb.selected_inferior().threads():
            c = cpu(cpu_thread)
            cpu_list[c.id] = c
        self.cpu_list = cpu_list

    def load_thread_list(self):
        self.thread_list = sorted(unordered_map(gdb.lookup_global_symbol('sched::thread_map').value()), key=lambda x: int(x["_id"]))

    def cpu_from_thread(self, thread):
        stack = thread['_attr']['stack']
        stack_begin = ulong(stack['begin'])
        stack_size = ulong(stack['size'])
        for c in self.cpu_list.viewvalues():
            if c.rsp > stack_begin and c.rsp <= stack_begin + stack_size:
                return c
        return None

class thread_context(object):
    def __init__(self, thread, state):
        self.old_frame = gdb.selected_frame()
        self.new_frame = gdb.newest_frame()
        self.new_frame.select()
        self.old_rsp = gdb.parse_and_eval('$rsp').cast(ulong_type)
        self.old_rip = gdb.parse_and_eval('$rip').cast(ulong_type)
        self.old_rbp = gdb.parse_and_eval('$rbp').cast(ulong_type)
        self.running_cpu = state.cpu_from_thread(thread)
        self.vm_thread = gdb.selected_thread()
        if not self.running_cpu:
            self.old_frame.select()
            self.new_rsp = thread['_state']['rsp'].cast(ulong_type)
            self.new_rip = thread['_state']['rip'].cast(ulong_type)
            self.new_rbp = thread['_state']['rbp'].cast(ulong_type)
    def __enter__(self):
        self.new_frame.select()
        if not self.running_cpu:
            gdb.execute('set $rsp = %s' % self.new_rsp)
            gdb.execute('set $rip = %s' % self.new_rip)
            gdb.execute('set $rbp = %s' % self.new_rbp)
        else:
            self.running_cpu.cpu_thread.switch()
    def __exit__(self, *_):
        if not self.running_cpu:
            gdb.execute('set $rsp = %s' % self.old_rsp)
            gdb.execute('set $rip = %s' % self.old_rip)
            gdb.execute('set $rbp = %s' % self.old_rbp)
        else:
            self.vm_thread.switch()
        self.old_frame.select()

def exit_thread_context():
    global active_thread_context
    if active_thread_context:
        active_thread_context.__exit__()
        active_thread_context = None

timer_state_expired = gdb.parse_and_eval('sched::timer_base::expired')

def show_thread_timers(t):
    timer_list = intrusive_list(t['_active_timers'])
    if timer_list:
        gdb.write('  timers:')
        for timer in timer_list:
            expired = '*' if timer['_state'] == timer_state_expired else ''
            expiration = long(timer['_time']) / 1.0e9
            gdb.write(' %11.9f%s' % (expiration, expired))
        gdb.write('\n')

def get_function_name(frame):
    if frame.function():
        return frame.function().name
    else:
        return '??'

class osv_info_threads(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info threads',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, for_tty):
        exit_thread_context()
        state = vmstate()
        for t in state.thread_list:
            with thread_context(t, state):
                cpu = t['_cpu']
                tid = t['_id']
                fr = gdb.selected_frame()
                # Non-running threads have always, by definition, just called
                # a reschedule, and the stack trace is filled with reschedule
                # related functions (switch_to, schedule, wait_until, etc.).
                # Here we try to skip such functions and instead show a more
                # interesting caller which initiated the wait.
                file_blacklist = ["arch-switch.hh", "sched.cc", "sched.hh",
                                  "mutex.hh", "mutex.cc", "mutex.c", "mutex.h"]

                # Functions from blacklisted files which are interesting
                sched_thread_join = 'sched::thread::join()'
                function_whitelist = [sched_thread_join]

                fname = '??'
                function = '??'
                sal = fr.find_sal()
                while sal.symtab:
                    fname = sal.symtab.filename
                    function = get_function_name(fr)
                    is_whitelisted = function in function_whitelist
                    is_blacklisted = os.path.basename(fname) in file_blacklist

                    if is_whitelisted or not is_blacklisted:
                        break

                    fr = fr.older()
                    sal = fr.find_sal()

                if fname[:6] == "../../":
                            fname = fname[6:]

                status = str(t['_status']['_M_i']).replace('sched::thread::', '')
                gdb.write('%4d (0x%x) cpu%s %-10s %s at %s:%s vruntime %12g\n' %
                          (tid, ulong(t.address),
                           cpu['arch']['acpi_id'],
                           status,
                           function,
                           fname,
                           sal.line,
                           t['_runtime']['_Rtt'],
                           )
                          )

                if function == sched_thread_join:
                    gdb.write("\tjoining on %s\n" % fr.read_var("this"))

                show_thread_timers(t)

class osv_info_callouts(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info callouts',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, for_tty):
        c = str(gdb.lookup_global_symbol('callouts::_callouts').value())
        callouts = re.findall('\[([0-9]+)\] = (0x[0-9a-zA-Z]+)', c)
        
        gdb.write("%-5s%-40s%-40s%-30s%-10s\n" % ("id", "addr", "function", "abs time (ns)", "flags"))
        
        # We have a valid callout frame
        for desc in callouts:
            id = int(desc[0])
            addr = desc[1]
            callout = gdb.parse_and_eval('(struct callout *)' + addr)
            fname = callout['c_fn']
            
            # time
            t = int(callout['c_to_ns'])
            
            # flags
            CALLOUT_ACTIVE = 0x0002
            CALLOUT_PENDING = 0x0004
            CALLOUT_COMPLETED = 0x0020
            f = int(callout['c_flags'])
            
            flags = ("0x%04x " % f) + \
                    ("A" if (callout['c_flags'] & CALLOUT_ACTIVE) else "") + \
                    ("P" if (callout['c_flags'] & CALLOUT_PENDING) else "") + \
                    ("C" if (callout['c_flags'] & CALLOUT_COMPLETED) else "")
            
            # dispatch time ns  ticks callout function
            gdb.write("%-5d%-40s%-40s%-30s%-10s\n" %
                      (id, callout, fname, t, flags))
                
class osv_thread(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)
    def invoke(self, arg, for_tty):
        exit_thread_context()
        state = vmstate()
        thread = None
        for t in state.thread_list:
            if t.address.cast(ulong_type) == long(arg, 0):
                thread = t
            with thread_context(t, state):
                if t['_id'] == long(arg, 0):
                    thread = t
        if not thread:
            print 'Not found'
            return
        active_thread_context = thread_context(thread, state)
        active_thread_context.__enter__()

class osv_thread_apply(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread apply', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class osv_thread_apply_all(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread apply all', gdb.COMMAND_USER,
                             gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        exit_thread_context()
        state = vmstate()
        for t in state.thread_list:
            gdb.write('thread %s\n\n' % t.address)
            with thread_context(t, state):
                gdb.execute(arg, from_tty)
            gdb.write('\n')

def continue_handler(event):
    exit_thread_context()

gdb.events.cont.connect(continue_handler)

def setup_libstdcxx():
    gcc = external + '/gcc.bin'
    sys.path += [gcc + '/usr/share/gdb/auto-load/usr/lib64',
                 glob(gcc + '/usr/share/gcc-*/python')[0],
                 ]
    main = glob(gcc + '/usr/share/gdb/auto-load/usr/lib64/libstdc++.so.*.py')[0]
    execfile(main)

def sig_to_string(sig):
    '''Convert a tracepoing signature encoded in a u64 to a string'''
    ret = ''
    while sig != 0:
        ret += chr(sig & 255)
        sig >>= 8
    ret = ret.replace('p', '50p')
    return ret

def align_down(v, pagesize):
    return v & ~(pagesize - 1)

def align_up(v, pagesize):
    return align_down(v + pagesize - 1, pagesize)

class Trace:
    def __init__(self, tp, thread, time, cpu, data, backtrace=None):
        self.tp = tp
        self.thread = thread
        self.time = time
        self.cpu = cpu
        self.data = data
        self.backtrace = backtrace

    @property
    def name(self):
        return self.tp['name'].string()

    def format_data(self):
        format = self.tp['format'].string()
        format = format.replace('%p', '0x%016x')
        return format % self.data

class TimedTrace:
    def __init__(self, trace):
        self.trace = trace
        self.duration = None

    @property
    def duration(self):
        return self.duration

class TraceConstants:
    def __init__(self):
        self.backtrace_len = ulong(gdb.parse_and_eval('tracepoint_base::backtrace_len'))
        self.bt_format = '   [' + str.join(' ', ['%s'] * self.backtrace_len) + ']'

class BacktraceFormatter:
    def __init__(self, trace_constants):
        self.trace_constants = trace_constants

    def __call__(self, backtrace):
        if not backtrace:
            return ''
        return self.trace_constants.bt_format % tuple([syminfo(x) for x in backtrace])

def all_traces():
    constants = TraceConstants()
    inf = gdb.selected_inferior()
    trace_log = gdb.lookup_global_symbol('trace_log').value()
    max_trace = ulong(gdb.parse_and_eval('max_trace'))
    trace_log = inf.read_memory(trace_log.address, max_trace)
    trace_page_size = ulong(gdb.parse_and_eval('trace_page_size'))
    last = ulong(gdb.lookup_global_symbol('trace_record_last').value()['_M_i'])
    last %= max_trace
    pivot = align_up(last, trace_page_size)
    trace_log = trace_log[pivot:] + trace_log[:pivot]
    last += max_trace - pivot
    backtrace_len = constants.backtrace_len

    i = 0
    while i < last:
        tp_key, thread, time, cpu, flags = struct.unpack('QQQII', trace_log[i:i+32])
        if tp_key == 0:
            i = align_up(i + 8, trace_page_size)
            continue
        tp = gdb.Value(tp_key).cast(gdb.lookup_type('tracepoint_base').pointer())
        sig = sig_to_string(ulong(tp['sig'])) # FIXME: cache
        i += 32

        backtrace = None
        if flags & 1:
            backtrace = struct.unpack('Q' * backtrace_len, trace_log[i:i+8*backtrace_len])
            i += 8 * backtrace_len

        size = struct.calcsize(sig)
        data = struct.unpack(sig, trace_log[i:i+size])
        i += size
        i = align_up(i, 8)
        yield Trace(tp, thread, time, cpu, data, backtrace=backtrace)

def nanos_to_millis(nanos):
    return float(nanos) / 1000000

def nanos_to_seconds(nanos):
    return float(nanos) / 1000000000

def format_duration(time):
    return "%4.3f" % nanos_to_millis(time)

def format_time(time):
    return "%12.6f" % nanos_to_seconds(time)

def dump_trace(out_func):
    indents = defaultdict(int)
    constants = TraceConstants()
    bt_formatter = BacktraceFormatter(constants)

    def lookup_tp(name):
        return gdb.lookup_global_symbol(name).value().dereference()
    tp_fn_entry = lookup_tp('gdb_trace_function_entry')
    tp_fn_exit = lookup_tp('gdb_trace_function_exit')

    for trace in all_traces():
        thread = trace.thread
        time = trace.time
        cpu = trace.cpu
        tp = trace.tp

        def trace_function(indent, annotation, data):
            fn, caller = data
            try:
                block = gdb.block_for_pc(long(fn))
                fn_name = block.function.print_name
            except:
                fn_name = '???'
            out_func('0x%016x %2d %19s %s %s %s\n'
                      % (thread,
                         cpu,
                         format_time(time),
                         indent,
                         annotation,
                         fn_name,
                         ))

        if tp == tp_fn_entry.address:
            indent = '  ' * indents[thread]
            indents[thread] += 1
            trace_function(indent, '->', trace.data)
        elif tp == tp_fn_exit.address:
            indents[thread] -= 1
            if indents[thread] < 0:
                indents[thread] = 0
            indent = '  ' * indents[thread]
            trace_function(indent, '<-', trace.data)
        else:
            out_func('0x%016x %2d %19s %-20s %s%s\n'
                      % (thread,
                         cpu,
                         format_time(time),
                         trace.name,
                         trace.format_data(),
                         bt_formatter(trace.backtrace),
                         )
                      )

def get_name_of_ended_func(name):
        m = re.match('(?P<func>.*)(_ret|_err)', name)
        if not m:
            return None

        return m.group('func')

# Block tracepoint begins a code block and is accompanied with
# tracepoints for block exit whose names end with _ret and _err
def get_block_tracepoints():
    block_tracepoints = set()
    for trace_point in intrusive_list(gdb.lookup_global_symbol("tracepoint_base::tp_list").value()):
        ended = get_name_of_ended_func(trace_point['name'].string())
        if ended:
            block_tracepoints.add(ended)
    return block_tracepoints

def get_timed_traces_per_function():
    block_tracepoints = get_block_tracepoints()

    # [thread][func] => TimedTrace
    open_functions = defaultdict(dict)

    # [func] => [TimedTrace, ...]
    timed_traces_per_function = defaultdict(list)

    for trace in all_traces():
        name = trace.tp['name'].string()
        ended = get_name_of_ended_func(name)
        if ended:
            if ended in open_functions[trace.thread]:
                timed = open_functions[trace.thread].pop(ended)
                timed.duration = trace.time - timed.trace.time
                timed_traces_per_function[ended].append(timed)
        elif name in block_tracepoints:
            if name in open_functions[trace.thread]:
                raise Exception("Nested traces not supported: " + name)
            open_functions[trace.thread][name] = TimedTrace(trace)

    return timed_traces_per_function

def get_percentile(sorted_samples, fraction):
    return sorted_samples[int(math.ceil(float(len(sorted_samples) - 1) * fraction))]

def dump_trace_summary(out_func):
    format = "%-20s %8s %8s %8s %8s %8s %8s %8s %8s\n"
    out_func("Execution times [ms]:\n")
    out_func(format % ("name", "count", "min", "50%", "90%", "99%", "99.9%", "max", "total"))

    for name, traces in get_timed_traces_per_function().iteritems():
        samples = sorted(map(operator.attrgetter('duration'), traces))
        out_func(format % (
            name,
            len(samples),
            format_duration(get_percentile(samples, 0)),
            format_duration(get_percentile(samples, 0.5)),
            format_duration(get_percentile(samples, 0.9)),
            format_duration(get_percentile(samples, 0.99)),
            format_duration(get_percentile(samples, 0.999)),
            format_duration(get_percentile(samples, 1)),
            format_duration(sum(samples)),
            ))

def dump_timed_trace(out_func, func=None):
    bt_formatter = BacktraceFormatter(TraceConstants())

    if func:
        traces = get_timed_traces_per_function()[func]
    else:
        traces = list()
        for trace_list in get_timed_traces_per_function().itervalues():
            traces.extend(trace_list)

    for timed in sorted(traces, key=lambda t: -t.duration):
        trace = timed.trace
        out_func('0x%016x %2d %20s %7s %-20s %s%s\n'
                    % (trace.thread,
                        trace.cpu,
                        format_time(trace.time),
                        format_duration(timed.duration),
                        trace.name,
                        trace.format_data(),
                        bt_formatter(trace.backtrace),
                        ))

def set_leak(val):
    gdb.parse_and_eval('memory::tracker_enabled=%s' % val)

def show_leak():
    tracker = gdb.parse_and_eval('memory::tracker')
    size_allocations = tracker['size_allocations']
    allocations = tracker['allocations']
    # Build a list of allocations to be sorted lexicographically by call chain
    # and summarize allocations with the same call chain:
    percent='   ';
    gdb.write('Fetching data from qemu/osv: %s' % percent);
    gdb.flush();
    allocs = [];
    for i in range(size_allocations) :
        newpercent = '%2d%%' % round(100.0*i/(size_allocations-1));
        if newpercent != percent :
            percent = newpercent;
            gdb.write('\b\b\b%s' % newpercent);
            gdb.flush();
        a = allocations[i]
        addr = ulong(a['addr'])
        if addr == 0 :
            continue
        nbacktrace = a['nbacktrace']
        backtrace = a['backtrace']
        callchain = []
        for j in range(nbacktrace) :
            callchain.append(ulong(backtrace[nbacktrace-1-j]))
        allocs.append((i, callchain))
    gdb.write('\n');

    gdb.write('Merging %d allocations by identical call chain... ' %
              len(allocs))
    gdb.flush();
    allocs.sort(key=lambda entry: entry[1])
    
    import collections
    Record = collections.namedtuple('Record',
                                    ['bytes', 'allocations', 'minsize',
                                     'maxsize', 'avgsize', 'minbirth',
                                     'maxbirth', 'avgbirth', 'callchain'])
    records = [];
    
    total_size = 0
    cur_n = 0
    cur_total_size = 0
    cur_total_seq = 0
    cur_first_seq = -1
    cur_last_seq = -1
    cur_max_size = -1
    cur_min_size = -1
    for k, alloc in enumerate(allocs) :
        i = alloc[0]
        callchain = alloc[1]
        seq = ulong(allocations[i]['seq'])
        size = ulong(allocations[i]['size'])
        total_size += size
        cur_n += 1
        cur_total_size += size
        cur_total_seq += seq
        if cur_first_seq<0 or seq<cur_first_seq :
            cur_first_seq = seq
        if cur_last_seq<0 or seq>cur_last_seq :
            cur_last_seq = seq
        if cur_min_size<0 or size<cur_min_size :
            cur_min_size = size
        if cur_max_size<0 or size>cur_max_size :
            cur_max_size = size
        # If the next entry has the same call chain, just continue summing
        if k!=len(allocs)-1 and callchain==allocs[k+1][1] :
            continue;
        # We're done with a bunch of allocations with same call chain:
        r = Record(bytes = cur_total_size,
                   allocations = cur_n,
                   minsize = cur_min_size,
                   maxsize = cur_max_size,
                   avgsize = cur_total_size/cur_n,
                   minbirth = cur_first_seq,
                   maxbirth = cur_last_seq,
                   avgbirth = cur_total_seq/cur_n,
                   callchain = callchain)
        records.append(r)
        cur_n = 0
        cur_total_size = 0
        cur_total_seq = 0
        cur_first_seq = -1
        cur_last_seq = -1
        cur_max_size = -1
        cur_min_size = -1
    gdb.write('generated %d records.\n' % len(records))
        
    # Now sort the records by total number of bytes
    records.sort(key=lambda r: r.bytes, reverse=True)

    gdb.write('\nAllocations still in memory at this time (seq=%d):\n\n' %
              tracker['current_seq'])
    for r in records :
        gdb.write('Found %d bytes in %d allocations [size ' % (r.bytes, r.allocations))
        if r.minsize != r.maxsize :
            gdb.write('%d/%.1f/%d' % (r.minsize, r.avgsize, r.maxsize))
        else :
            gdb.write('%d' % r.minsize)
        gdb.write(', birth ')
        if r.minbirth != r.maxbirth :
            gdb.write('%d/%.1f/%d' % (r.minbirth, r.avgbirth, r.maxbirth))
        else :
            gdb.write('%d' % r.minbirth)
        gdb.write(']\nfrom:\n')
        for f in reversed(r.callchain):
            si = syminfo(f)
            gdb.write('\t%s\n' % (si,))
        gdb.write('\n')

def drivers():
    drvman = gdb.lookup_global_symbol('hw::driver_manager::_instance').value()
    drivers = drvman['_drivers']
    return read_vector(drivers)

def show_virtio_driver(v):
    gdb.write('%s at %s\n' % (v.dereference().dynamic_type, v))
    vb = v.cast(virtio_driver_type.pointer())
    for qidx in range(0, vb['_num_queues']):
        q = vb['_queues'][qidx]
        gdb.write('  queue %d at %s\n' % (qidx, q))
        avail_guest_idx = q['_avail']['_idx']['_M_i']
        avail_host_idx = q['_avail_event']['_M_i']
        gdb.write('    avail g=0x%x h=0x%x (%d)\n'
                  % (avail_host_idx, avail_guest_idx, avail_guest_idx - avail_host_idx))
        used_host_idx = q['_used']['_idx']['_M_i']
        used_guest_idx = q['_used_event']['_M_i']
        gdb.write('    used   h=0x%x g=0x%x (%d)\n'
                  % (used_host_idx, used_guest_idx, used_host_idx - used_guest_idx))
        used_flags = q['_used']['_flags']['_M_i']
        gdb.write('    used notifications: %s\n' %
                  ('disabled' if used_flags & 1 else 'enabled',))

class osv_trace(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace', gdb.COMMAND_USER, gdb.COMPLETE_COMMAND, True)
    def invoke(self, arg, from_tty):
        dump_trace(gdb.write)

class osv_trace_summary(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace summary', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        dump_trace_summary(gdb.write)

class osv_trace_duration(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace duration', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        dump_timed_trace(gdb.write, arg)

class osv_trace_file(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace2file', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        fout = file("trace.txt", "wt")
        dump_trace(fout.write)
        fout.close()

class osv_leak(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class osv_leak_show(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak show', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        show_leak()

class osv_leak_on(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak on', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        set_leak('true')

class osv_leak_off(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak off', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        set_leak('false')

class osv_pagetable(gdb.Command):
    '''Commands for examining the page table'''
    def __init__(self):
        gdb.Command.__init__(self, 'osv pagetable', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class osv_pagetable_walk(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv pagetable walk',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        addr = gdb.parse_and_eval(arg)
        addr = ulong(addr)
        ptep = ulong(gdb.lookup_global_symbol('mmu::page_table_root').value().address)
        level = 4
        while level >= 0:
            ptep1 = phys_cast(ptep, ulong_type)
            pte = ulong(ptep1.dereference())
            gdb.write('%016x %016x\n' % (ptep, pte))
            if not pte & 1:
                break
            if level > 0 and pte & 0x80:
                break
            if level > 0:
                pte &= ~ulong(0x80)
            pte &= ~ulong(0x8000000000000fff)
            level -= 1
            ptep = pte + pt_index(addr, level) * 8

def runqueue(cpuid, node = None):
    if (node == None):
        cpus = gdb.lookup_global_symbol('sched::cpus').value()
        cpu = cpus['_M_impl']['_M_start'][cpuid]
        rq = cpu['runqueue']
        p = rq['data_']['node_plus_pred_']
        node = p['header_plus_size_']['header_']['parent_']

    if (long(node) != 0):
        offset = gdb.parse_and_eval('(int)&((sched::thread *)0)->_runqueue_link');
        thread = node.cast(gdb.lookup_type('void').pointer()) - offset
        thread = thread.cast(gdb.lookup_type('sched::thread').pointer())

        for x in runqueue(cpuid, node['left_']):
            yield x

        yield thread

        for x in runqueue(cpuid, node['right_']):
            yield x

class osv_runqueue(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv runqueue',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        ncpus = gdb.parse_and_eval('sched::cpus._M_impl._M_finish - sched::cpus._M_impl._M_start');
        for cpu in xrange(ncpus) :
            gdb.write("CPU %d:\n" % cpu)
            for thread in runqueue(cpu):
                print '%d 0x%x %g' % (thread['_id'], ulong(thread), thread['_runtime']['_Rtt'])

class osv_info_virtio(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info virtio', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        for driver in drivers():
            if derived_from(driver.dereference().dynamic_type, virtio_driver_type):
                show_virtio_driver(driver)


osv()
osv_heap()
osv_memory()
osv_mmap()
osv_syms()
osv_info()
osv_info_threads()
osv_info_callouts()
osv_info_virtio()
osv_thread()
osv_thread_apply()
osv_thread_apply_all()
osv_trace()
osv_trace_summary()
osv_trace_duration()
osv_trace_file()
osv_leak()
osv_leak_show()
osv_leak_on()
osv_leak_off()
osv_pagetable()
osv_pagetable_walk()
osv_runqueue()

setup_libstdcxx()
