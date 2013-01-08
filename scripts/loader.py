#!/usr/bin/python

import gdb
import re
import os

args = ''
text_addr = '?'
libjvm = '/usr/lib/jvm/java-1.7.0-openjdk.x86_64/jre/lib/amd64/server/libjvm.so'
unwanted_sections = ['.text',
                     '.note.stapsdt',
                     '.gnu_debuglink',
                     '.gnu_debugdata',
                     '.shstrtab',
                     ]
for line in os.popen('readelf -WS ' + libjvm):
    m = re.match(r'\s*\[ *\d+\]\s+([\.\w\d_]+)\s+\w+\s+([0-9a-f]+).*', line)
    if m:
        section = m.group(1)
        if section == 'NULL':
            continue
        addr = hex(int(m.group(2), 16) + 0x100000000000)
        if section == '.text':
            text_addr = addr
        if section not in unwanted_sections:
            args += ' -s %s %s' % (section, addr)

gdb.execute('add-symbol-file %s %s %s' % (libjvm, text_addr, args))

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

osv()
osv_heap()
