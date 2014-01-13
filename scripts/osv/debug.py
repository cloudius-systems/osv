import os
import re
import subprocess

class SourceAddress:
    def __init__(self, addr, name, filename, line):
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
        src_addr = self.cache.get(addr, None)
        if not src_addr:
            src_addr = SourceAddress(addr, None, None, None)
            self.cache[addr] = src_addr
        return src_addr

class SymbolResolver(object):
    def __init__(self, object_path):
        if not os.path.exists(object_path):
            raise Exception('File not found: ' + object_path)
        self.addr2line = subprocess.Popen(['addr2line', '-e', object_path, '-Cfp'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.cache = {}

    def __call__(self, addr):
        """
        Returns SourceAddress for given addr.

        """
        src_addr = self.cache.get(addr, None)
        if src_addr:
            return src_addr

        self.addr2line.stdin.write('0x%x\n' % addr)
        response = self.addr2line.stdout.readline().rstrip('\n')

        # When -f is passed to addr2line, "??"" line precedes "??:0" line. We need to consume both.
        m = re.match(r'^\?\?$', response)
        if m:
            response = self.addr2line.stdout.readline().rstrip('\n')
            if not re.match(r'^\?\?:0$', response):
                raise Exception('Unexpected response: ' + response)
            src_addr = SourceAddress(addr, None, None, None)
        else:
            m = re.match(r'(?P<name>.*) at ((?P<file>.*?)|\?+):((?P<line>\d+)|\?+)', response)
            if not m:
                raise Exception('addr2line response not matched: ' + response)
            src_addr = SourceAddress(addr, m.group('name'), m.group('file'), m.group('line'))

        self.cache[addr] = src_addr
        return src_addr

    def close():
        self.addr2line.stdin.close()
        self.addr2line.wait()
