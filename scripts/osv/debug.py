import os
import re
import subprocess
import threading
import itertools
import select

class SourceAddress:
    def __init__(self, addr, name=None, filename=None, line=None):
        self.addr = addr
        self.name = name
        self.filename = filename
        self.line = line

    def __str__(self):
        if self.name:
            return self.name
        return '0x%x' % self.addr

class DummyResolver(object):
    def __init__(self):
        self.cache = {}

    def __call__(self, addr):
        result = self.cache.get(addr, None)
        if not result:
            result = [SourceAddress(addr)]
            self.cache[addr] = result
        return result

class SymbolResolver(object):
    inline_prefix = ' (inlined by) '

    def __init__(self, object_path, show_inline=True):
        if not os.path.exists(object_path):
            raise Exception('File not found: ' + object_path)
        self.show_inline = show_inline
        flags = '-Cfp'
        if show_inline:
            flags += 'i'
        self.addr2line = subprocess.Popen(['addr2line', '-e', object_path, flags],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.cache = {}

    def next_line(self):
        return self.addr2line.stdout.readline().rstrip('\n')

    def consume_unknown(self, line):
        # addr2line ver. 2.23.2 (Ubuntu)
        m = re.match(r'^\?\?$', line)
        if m:
            line = self.next_line()
            if not re.match(r'^\?\?:0$', line):
                raise Exception('Unexpected response: ' + line)
            return True

        # addr2line ver. 2.23.52.0.1-9.fc19
        m = re.match(r'^\?\? \?\?:0$', line)
        if m:
            return True

    def parse_line(self, addr, line):
        if self.consume_unknown(line):
            return SourceAddress(addr)

        m = re.match(r'(?P<name>.*) at ((?P<file>.*?)|\?+):((?P<line>\d+)|\?+)', line)
        if not m:
            raise Exception('addr2line response not matched: ' + line)
        return SourceAddress(addr, m.group('name'), m.group('file'), m.group('line'))

    def __call__(self, addr):
        """
        Returns an iterable of SourceAddress objects for given addr.

        """
        result = self.cache.get(addr, None)
        if result:
            return result

        self.addr2line.stdin.write('0x%x\n' % addr)

        if self.show_inline:
            self.addr2line.stdin.write('0\n')

        result = [self.parse_line(addr, self.next_line())]

        if self.show_inline:
            line = self.next_line()
            while line.startswith(self.inline_prefix):
                result.append(self.parse_line(addr, line[len(self.inline_prefix):]))
                line = self.next_line()
            self.consume_unknown(line)

        self.cache[addr] = result
        return result

    def close():
        self.addr2line.stdin.close()
        self.addr2line.wait()

def resolve_all(resolver, raw_addresses):
    """
    Returns iterable of SourceAddress objects for given list of raw addresses
    using supplied resolver.

    """
    return itertools.chain.from_iterable(map(resolver, raw_addresses))
