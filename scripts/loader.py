#!/usr/bin/python

import gdb
import re
import os, os.path
import struct

build_dir = os.path.dirname(gdb.current_objfile().filename)
external = build_dir + '/../../external'

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
        print page_range, page_range['size']
        self.show(node['right_'])

def ulong(x):
    if x < 0:
        print x
        x += 1L << 64
        print x
    return x

ulong_type = gdb.lookup_type('unsigned long')

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

class vmstate(object):
    def __init__(self):
        self.reload()
    def reload(self):
        self.load_thread_list()
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

class thread_context(object):
    def __init__(self, thread):
        self.old_frame = gdb.selected_frame()
        self.new_frame = gdb.newest_frame()
        self.new_frame.select()
        self.running = (not long(thread['_on_runqueue'])
                        and not long(thread['_waiting']['_M_base']['_M_i']))
        self.old_rsp = ulong(gdb.parse_and_eval('$rsp').cast(ulong_type))
        self.old_rip = ulong(gdb.parse_and_eval('$rip').cast(ulong_type))
        self.old_rbp = ulong(gdb.parse_and_eval('$rbp').cast(ulong_type))
        if not self.running:
            self.old_frame.select()
            self.new_rsp = thread['_state']['rsp'].cast(ulong_type)
    def __enter__(self):
        self.new_frame.select()
        if not self.running:
            gdb.execute('set $rsp = %s' % (self.new_rsp + 16))
            inf = gdb.selected_inferior()
            stack = inf.read_memory(self.new_rsp, 16)
            (new_rip, new_rbp) = struct.unpack('qq', stack)
            gdb.execute('set $rip = %s' % (new_rip + 1))
            gdb.execute('set $rbp = %s' % new_rbp)
    def __exit__(self, *_):
        if not self.running:
            gdb.execute('set $rsp = %s' % self.old_rsp)
            gdb.execute('set $rip = %s' % self.old_rip)
            gdb.execute('set $rbp = %s' % self.old_rbp)
        self.old_frame.select()

class osv_info_threads(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info threads',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, for_tty):
        state = vmstate()
        for t in state.thread_list:
            with thread_context(t):
                cpu = t['_cpu']
                fr = gdb.selected_frame()
                sal = fr.find_sal()
                status = 'rdy '
                if long(t['_waiting']['_M_base']['_M_i']):
                    status = 'wait'
                function = '??'
                if fr.function():
                    function = fr.function().name
                fname = '??'
                if sal.symtab:
                    fname = sal.symtab.filename
                gdb.write('%s cpu%s %s %s at %s:%s\n' %
                          (ulong(t.address),
                           cpu['arch']['acpi_id'],
                           status,
                           function,
                           fname,
                           sal.line
                           )
                          )

class osv_thread(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)
    #def invoke(self, arg, for_tty):
    #    for t in thread_list():
    #        if t.address.cast(ulong_type) == long(arg, 0):
    #            print 'match'

class osv_thread_apply(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread apply', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class osv_thread_apply_all(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread apply all', gdb.COMMAND_USER,
                             gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        state = vmstate()
        for t in state.thread_list:
            gdb.write('thread %s\n\n' % t.address)
            with thread_context(t):
                gdb.execute(arg, from_tty)
            gdb.write('\n')


osv()
osv_heap()
osv_syms()
osv_info()
osv_info_threads()
osv_thread()
osv_thread_apply()
osv_thread_apply_all()