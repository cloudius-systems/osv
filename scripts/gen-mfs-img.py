#!/usr/bin/python

import os, optparse, io, subprocess, socket, threading, stat, sys, shutil
from struct import *
from ctypes import *
try:
    import configparser
except ImportError:
    import ConfigParser as configparser


OSV_BLOCK_SIZE = 512

DIR_MODE = int('0x4000', 16)
REG_MODE = int('0x8000', 16)

block = 0

class SuperBlock(Structure):
    _fields_ = [
        ('version', c_ulonglong),
        ('magic', c_ulonglong),
        ('block_size', c_ulonglong),
        ('inodes_block', c_ulonglong)
    ]


class Inode(Structure):
    _fields_ = [
        ('mode', c_ulonglong),
        ('inode_no', c_ulonglong),
        ('data_block_number', c_ulonglong),
        ('count', c_ulonglong) # either file size or children count
    ]

class Record(Structure):
    _fields_ = [
        ('filename', c_char * 64),
        ('inode_no', c_ulonglong)
    ]


inodes = []
inodes_count = 1

def nextInode():
    global inodes_count
    global inodes

    inode = Inode()
    inode.inode_no = inodes_count
    inodes_count += 1
    inodes.append(inode)

    return inode


def pad(fp, size):
    fp.write('\0' * size)
    return size


def write_initial_superblock(fp):
    global block
    pad(fp, OSV_BLOCK_SIZE) # superblock is empty at first
    block += 1


def writefile(fp, path):
    global block

    total = 0
    last = 0

    with open(path, 'rb') as f:
        while True:
            chunk = f.read(OSV_BLOCK_SIZE)
            if chunk:
                last = len(chunk)
                total += last
                block += 1
                fp.write(chunk)
            else:
                break

    pad(fp, OSV_BLOCK_SIZE - last)

    return total


def writeArray(fp, vals, size):
    global block
    c = 0
    perBlock = OSV_BLOCK_SIZE / size
    padding = OSV_BLOCK_SIZE - (perBlock * size)
    blocksNeeded = (len(vals) / perBlock) + (1 if len(vals) % perBlock > 0 else 0)

    for v in vals:
        fp.write(v)
        c += 1
        if c == perBlock:
            c = 0
            pad(fp, padding)

    block += blocksNeeded

    if c != 0:
        pad(fp, OSV_BLOCK_SIZE - (c * size))

def writedir(fp, manifest):
    global block
    records = []
    def nextRecord():
        rec = Record()
        records.append(rec)
        return rec

    for entry in manifest:
        if len(entry) > 63:
            continue

        val = manifest.get(entry)

        inode = nextInode()
        rec = nextRecord()

        rec.inode_no = inode.inode_no
        rec.filename = entry

        if type(val) is dict: # folder
            inode.mode = DIR_MODE
            count, block_no = writedir(fp, val)
            inode.count = count
            inode.data_block_number = block_no
        else: # file
            inode.mode = REG_MODE
            inode.data_block_number = block
            inode.count = writefile(fp, val)


    block_no = block
    writeArray(fp, records, sizeof(Record))
    return (len(records), block_no)


def writefs(fp, manifest):
    global block
    global inodes

    root_inode = nextInode()
    root_inode.mode = DIR_MODE
    
    count, block_no = writedir(fp, manifest.get(''))
    root_inode.count = count
    root_inode.data_block_number = block_no

    # Write inodes!
    block_no = block
    writeArray(fp, inodes, sizeof(Inode))

    return block_no


def genImage(out, manifest):
    fp = open(out, 'wb')

    # write the initial superblock
    write_initial_superblock(fp)

    inodes_block = writefs(fp, manifest)

    sb = SuperBlock()
    sb.version = 1
    sb.magic = int('0xDEADBEEF', 16)
    sb.block_size = OSV_BLOCK_SIZE
    sb.inodes_block = inodes_block

    fp.seek(0)
    fp.write(sb)

    fp.close()


defines = {}

def add_var(option, opt, value, parser):
    var, val = value.split('=')
    defines[var] = val

def expand(items):
    for name, hostname in items:
        if name.endswith('/**') and hostname.endswith('/**'):
            name = name[:-2]
            hostname = hostname[:-2]
            for dirpath, dirnames, filenames in os.walk(hostname):
                for filename in filenames:
                    relpath = dirpath[len(hostname):]
                    if relpath != "":
                        relpath += "/"
                    yield (name + relpath + filename,
                           hostname + relpath + filename)
        elif '/&/' in name and hostname.endswith('/&'):
            prefix, suffix = name.split('/&/', 1)
            yield (prefix + '/' + suffix, hostname[:-1] + suffix)
        else:
            yield (name, hostname)

def unsymlink(f):
    if f.startswith('!'):
        return f[1:]
    if f.startswith('->'):
        return f
    try:
        link = os.readlink(f)
        if link.startswith('/'):
            # try to find a match
            base = os.path.dirname(f)
            while not os.path.exists(base + link):
                if base == '/':
                    return f
                base = os.path.dirname(base)
        else:
            base = os.path.dirname(f) + '/'
        return unsymlink(base + link)
    except Exception:
        return f

def parseManifest(manifest):
    files = dict([(f, manifest.get('manifest', f, vars=defines))
                  for f in manifest.options('manifest')])

    files = list(expand(files.items()))
    files = [(x, unsymlink(y)) for (x, y) in files]

    file_dict = {}

    for name, hostname in files:
        if hostname.startswith('->'): # Ignore links for the time being
            pass
        else:
            print "Adding %s" % name
            if os.path.isdir(hostname):
                p = file_dict
                for token in name.split('/'):
                    p = p.setdefault(token, {})
            elif os.path.isfile(hostname):
                dirname = os.path.dirname(name)
                basename = os.path.basename(name)
                p = file_dict
                if dirname == '/':
                    p = p.setdefault('', {})
                else:
                    for token in dirname.split('/'):
                        p = p.setdefault(token, {})
                p[basename] = hostname

    return file_dict


def main():
    make_option = optparse.make_option

    opt = optparse.OptionParser(option_list=[
            make_option('-o',
                        dest='output',
                        help='write to FILE',
                        metavar='FILE'),
            make_option('-m',
                        dest='manifest',
                        help='read manifest from FILE',
                        metavar='FILE'),
            make_option('-D',
                        type='string',
                        help='define VAR=DATA',
                        metavar='VAR=DATA',
                        action='callback',
                        callback=add_var),
    ])

    (options, args) = opt.parse_args()

    manifest = configparser.SafeConfigParser()
    manifest.optionxform = str # avoid lowercasing
    manifest.read(options.manifest)

    outfile = os.path.abspath(options.output)

    manifest = parseManifest(manifest)

    genImage(outfile, manifest)

if __name__ == "__main__":
    main()
