#!/usr/bin/env python
import sys
import argparse
from osv import trace
from osv import debug

class SymbolPrinter:
    def __init__(self, resolver):
        self.resolver = resolver

    def __call__(self, addr):
        return self.resolver(addr).name or trace.simple_symbol_formatter(addr)

def add_symbol_resolution_options(parser):
    parser.add_argument("-d", "--debug", action="store_true", help="use loader.elf from debug build")
    parser.add_argument("-e", "--exe", action="store", help="path to the object file used for symbol resolution")

def get_backtrace_formatter(args):
    if not args.backtrace:
        return lambda backtrace: ''

    if args.exe:
        elf_path = args.exe
    elif args.debug:
        elf_path = 'build/debug/loader.elf'
    else:
        elf_path = 'build/release/loader.elf'

    symbol_resolver = debug.SymbolResolver(elf_path)
    symbol_printer = SymbolPrinter(symbol_resolver)
    return trace.BacktraceFormatter(symbol_printer)

def list_trace(args):
    backtrace_formatter = get_backtrace_formatter(args)
    with trace.read_file(args.tracefile) as reader:
        for t in reader.get_traces():
            print t.format(backtrace_formatter)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="trace file processing")

    subparsers = parser.add_subparsers(help="Command")

    cmd_list = subparsers.add_parser("list", help="List trace")
    add_symbol_resolution_options(cmd_list)
    parser.add_argument("-b", "--backtrace", action="store_true", help="show backtrace")

    cmd_list.add_argument("tracefile", help="Path to trace file")
    cmd_list.set_defaults(func=list_trace)

    args = parser.parse_args()
    args.func(args)
