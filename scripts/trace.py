#!/usr/bin/env python
import sys
import argparse
from osv import trace, debug

class symbol_printer:
    def __init__(self, resolver, formatter):
        self.resolver = resolver
        self.formatter = formatter

    def __call__(self, addr):
        return self.formatter(self.resolver(addr))

class src_addr_formatter:
    def __init__(self, args):
        self.args = args

    def __call__(self, src_addr):
        if not src_addr.name:
            return '0x%x' % src_addr.addr

        text = src_addr.name

        if self.args.show_file_name:
            text += ' %s' % src_addr.filename

        if self.args.show_line_number:
            text += ':%s' % src_addr.line

        if self.args.show_address:
            text += ' @ 0x%x' % src_addr.addr

        return text

def add_trace_source_options(parser):
    parser.add_argument("tracefile", help="Path to trace file")

def get_trace_reader(args):
    return trace.read_file(args.tracefile)

def add_symbol_resolution_options(parser):
    parser.add_argument("-d", "--debug", action="store_true", help="use loader.elf from debug build")
    parser.add_argument("-e", "--exe", action="store", help="path to the object file used for symbol resolution")
    parser.add_argument("-x", "--no-resolve", action='store_true', help="do not resolve symbols")
    parser.add_argument("-L", "--show-line-number", action='store_true', help="show line numbers")
    parser.add_argument("-A", "--show-address", action='store_true', help="show raw addresses")
    parser.add_argument("-F", "--show-file-name", action='store_true', help="show file names")

class BeautifyingResolver(object):
    def __init__(self, delegate):
        self.delegate = delegate

    def __call__(self, addr):
        src_addr = self.delegate(addr)
        if src_addr.name:
            if src_addr.name.startswith("void sched::thread::do_wait_until<"):
                src_addr.name = "sched::thread::do_wait_until"
            elif src_addr.name.startswith("do_wait_until<"):
                src_addr.name = "do_wait_until"
        return src_addr

def symbol_resolver(args):
    if args.no_resolve:
        return debug.DummyResolver()

    if args.exe:
        elf_path = args.exe
    elif args.debug:
        elf_path = 'build/debug/loader.elf'
    else:
        elf_path = 'build/release/loader.elf'

    return BeautifyingResolver(debug.SymbolResolver(elf_path))

def get_backtrace_formatter(args):
    if not args.backtrace:
        return lambda backtrace: ''

    return trace.BacktraceFormatter(
        symbol_printer(symbol_resolver(args), src_addr_formatter(args)))

def list_trace(args):
    backtrace_formatter = get_backtrace_formatter(args)
    with get_trace_reader(args) as reader:
        for t in reader.get_traces():
            print t.format(backtrace_formatter)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="trace file processing")

    subparsers = parser.add_subparsers(help="Command")

    cmd_list = subparsers.add_parser("list", help="List trace")
    add_symbol_resolution_options(cmd_list)
    cmd_list.add_argument("-b", "--backtrace", action="store_true", help="show backtrace")
    add_trace_source_options(cmd_list)
    cmd_list.set_defaults(func=list_trace)

    args = parser.parse_args()
    args.func(args)
