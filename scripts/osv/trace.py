import os
import mmap
import struct
import sys

def nanos_to_millis(nanos):
    return float(nanos) / 1000000

def nanos_to_seconds(nanos):
    return float(nanos) / 1000000000

def format_duration(time):
    return "%4.3f" % nanos_to_millis(time)

def format_time(time):
    return "%12.6f" % nanos_to_seconds(time)

class BacktraceFormatter:
    def __init__(self, resolver):
        self.resolver = resolver

    def __call__(self, backtrace):
        if not backtrace:
            return ''
        return '   [' + ' '.join((str(self.resolver(x)) for x in backtrace if x)) + ']'

def simple_symbol_formatter(addr):
    return '0x%x' % frame

default_backtrace_formatter = BacktraceFormatter(simple_symbol_formatter)

class TracePoint:
    def __init__(self, key, name, signature, format):
        self.key = key
        self.name = name
        self.signature = signature
        self.format = format

    def __repr__(self):
        return 'TracePoint(key=%s, name=%s, signature=%s, format=%s)' % (
                self.key,
                self.name,
                self.signature,
                self.format)

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
        return self.tp.name

    def format_data(self):
        format = self.tp.format
        format = format.replace('%p', '0x%016x')
        return format % self.data

    def format(self, bt_formatter=default_backtrace_formatter):
        return '0x%016x %2d %19s %-20s %s%s' % (
            self.thread,
            self.cpu,
            format_time(self.time),
            self.name,
            self.format_data(),
            bt_formatter(self.backtrace))

    def __str__(self):
        return self.format()
