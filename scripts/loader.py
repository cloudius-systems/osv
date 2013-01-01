#!/usr/bin/python

import gdb

gdb.execute('add-symbol-file /usr/lib/jvm/java-1.7.0-openjdk.x86_64/jre/lib/amd64/server/libjvm.so 0x100000195410')

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
