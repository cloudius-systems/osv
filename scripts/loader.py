#!/usr/bin/python

import gdb
import re
import os, os.path
import struct
import json
from glob import glob

build_dir = os.path.dirname(gdb.current_objfile().filename)
external = build_dir + '/../../external'

class status_enum_class(object):
    pass
status_enum = status_enum_class()

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

def translate(path):
    '''given a path, try to find it on the host OS'''
    name = os.path.basename(path)
    for top in [build_dir, external, '/usr']:
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

class osv(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv',
                             gdb.COMMAND_USER, gdb.COMPLETE_COMMAND, True)

class osv_heap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv heap',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        free_page_ranges = gdb.lookup_global_symbol('memory::free_page_ranges').value()
        p = free_page_ranges['tree_']['data_']['node_plus_pred_']
        p = p['header_plus_size_']['header_']['parent_']
        self.show(p)
    def show(self, node):
        if long(node) == 0:
            return
        page_range = node.cast(gdb.lookup_type('void').pointer()) - 8
        page_range = page_range.cast(gdb.lookup_type('memory::page_range').pointer())
        self.show(node['left_'])
        print '%s 0x%016x' % (page_range, page_range['size'])
        self.show(node['right_'])

ulong_type = gdb.lookup_type('unsigned long')
timer_type = gdb.lookup_type('sched::timer')

active_thread_context = None

def ulong(x):
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
        ret = []
        thread_list = gdb.lookup_global_symbol('sched::thread_list').value()
        root = thread_list['data_']['root_plus_size_']['root_']
        node = root['next_']
        thread_type = gdb.lookup_type('sched::thread')
        void_ptr = gdb.lookup_type('void').pointer()
        for f in thread_type.fields():
            if f.name == '_thread_list_link':
                link_offset = f.bitpos / 8
        while node != root.address:
            t = node.cast(void_ptr) - link_offset
            t = t.cast(thread_type.pointer())
            ret.append(t.dereference())
            node = node['next_']
        self.thread_list = ret
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
        self.old_rsp = ulong(gdb.parse_and_eval('$rsp').cast(ulong_type))
        self.old_rip = ulong(gdb.parse_and_eval('$rip').cast(ulong_type))
        self.old_rbp = ulong(gdb.parse_and_eval('$rbp').cast(ulong_type))
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

def show_thread_timers(t):
    head = t['_active_timers']['data_']['root_plus_size_']['root_']
    n = head['next_']
    if n == head.address:
        return
    gdb.write('  timers:')
    while n != head.address:
        na = n.cast(ulong_type)
        na -= timer_type.fields()[1].bitpos / 8
        timer = na.cast(timer_type.pointer())
        expired = ''
        if timer['_expired']:
            expired = '*'
        expiration = long(timer['_time']) / 1.0e9
        gdb.write(' %11.9f%s' % (expiration, expired))
        n = n['next_']
    gdb.write('\n')

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
                fname = '??'
                sal = fr.find_sal()
                while sal.symtab:
                    fname = sal.symtab.filename
                    b = os.path.basename(fname);
                    if b=="arch-switch.hh" or b=="sched.cc" or b=="sched.hh" or b=="mutex.hh":
                        fr = fr.older();
                        sal = fr.find_sal();
                    else:
                        break;

                if fr.function():
                    function = fr.function().name
		else:
                    function = '??'
                status = str(t['_status']['_M_i']).replace('sched::thread::', '')
                gdb.write('%4d (0x%x) cpu%s %-10s %s at %s:%s vruntime %12d\n' %
                          (tid, ulong(t.address),
                           cpu['arch']['acpi_id'],
                           status,
                           function,
                           fname,
                           sal.line,
                           t['_vruntime'],
                           )
                          )
                show_thread_timers(t)

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
    return ret

def align_down(v, pagesize):
    return v & ~(pagesize - 1)

def align_up(v, pagesize):
    return align_down(v + pagesize - 1, pagesize)

def dump_trace():
    from collections import defaultdict
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
    indents = defaultdict(int)
    def lookup_tp(name):
        tp_base = gdb.lookup_type('tracepoint_base')
        return gdb.lookup_global_symbol(name).value().cast(tp_base)
    tp_fn_entry = lookup_tp('trace_function_entry')
    tp_fn_exit = lookup_tp('trace_function_exit')

    i = 0
    while i < last:
        tp_key, thread = struct.unpack('QQ', trace_log[i:i+16])
        def trace_function(indent, annotation, data):
            fn, caller = data
            try:
                block = gdb.block_for_pc(long(fn))
                fn_name = block.function.print_name
            except:
                fn_name = '???'
            gdb.write('0x%016x %s %s %s\n' % (thread,
                                              indent,
                                              annotation,
                                              fn_name,
                                              ))
        if tp_key == 0:
            i = align_up(i, trace_page_size)
            continue
        tp = gdb.Value(tp_key).cast(gdb.lookup_type('tracepoint_base').pointer())
        sig = sig_to_string(ulong(tp['sig'])) # FIXME: cache
        i += 16
        size = struct.calcsize(sig)
        buffer = trace_log[i:i+size]
        i += size
        data = struct.unpack(sig, buffer)
        if tp == tp_fn_entry.address:
            indent = '  ' * indents[thread]
            indents[thread] += 1
            trace_function(indent, '->', data)
        elif tp == tp_fn_exit.address:
            indents[thread] -= 1
            if indents[thread] < 0:
                indents[thread] = 0
            indent = '  ' * indents[thread]
            trace_function(indent, '<-', data)
        else:
            format = tp['format'].string()
            format = format.replace('%p', '0x%016x')
            name = tp['name'].string()
            gdb.write('0x%016x %-20s %s\n' % (thread,
                                              name,
                                              format % data,
                                              )
                      )

class osv_trace(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        dump_trace()

osv()
osv_heap()
osv_syms()
osv_info()
osv_info_threads()
osv_thread()
osv_thread_apply()
osv_thread_apply_all()
osv_trace()

setup_libstdcxx()
