#!/usr/bin/python

import gdb
import re
import os

text_addr = '?'
libjvm = '/usr/lib/jvm/java-1.7.0-openjdk.x86_64/jre/lib/amd64/server/libjvm.so'
for line in os.popen('readelf -WS ' + libjvm):
    m = re.match(r'\s*\[ *\d+\]\s+([\.\w\d_]+)\s+\w+\s+([0-9a-f]+).*', line)
    if m:
        if m.group(1) == '.text':
            text_addr = hex(int(m.group(2), 16) + 0x100000000000)

gdb.execute('add-symbol-file %s %s' % (libjvm, text_addr))

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
