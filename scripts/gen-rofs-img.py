#!/usr/bin/python3

#
# Copyright (c) 2015 Carnegie Mellon University.
# All Rights Reserved.
#
# THIS SOFTWARE IS PROVIDED "AS IS," WITH NO WARRANTIES WHATSOEVER. CARNEGIE
# MELLON UNIVERSITY EXPRESSLY DISCLAIMS TO THE FULLEST EXTENT PERMITTEDBY LAW
# ALL EXPRESS, IMPLIED, AND STATUTORY WARRANTIES, INCLUDING, WITHOUT
# LIMITATION, THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE, AND NON-INFRINGEMENT OF PROPRIETARY RIGHTS.
#
# Released under a modified BSD license. For full terms, please see mfs.txt in
# the licenses folder or contact permi...@sei.cmu.edu.
#
# DM-0002621
#
#
# Copyright (C) 2017 Waldemar Kozaczuk
# Inspired by original MFS implementation by James Root from 2015
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#

##################################################################################
# The layout of data on the disk in the block order:
#
# Super Block (512 bytes) that contains magic number and specifies meta
# information including block size and location and size of tables containing
# i-nodes, dentries and symbolic links
#
# Files data where each file is padded to 512 bytes block
#
# Table of directory entries referenced by index in directory i-node
# (each entry holds string with direntry name and i-node number)
#
# Table of symlinks referenced by symlink i-node (each entry holds symbolic link
# path string)
#
# Table of inodes where each specifies type (dir,file,symlink) and data offset
# (for files it is a block on a disk, for symlinks and directories it is an
# offset in one of the 2 tables above)
##################################################################################

import os, optparse, io
from struct import *
from ctypes import *
from manifest_common import add_var, expand, unsymlink, read_manifest, defines, strip_file

OSV_BLOCK_SIZE = 512

DIR_MODE  = int('0x4000', 16)
REG_MODE  = int('0x8000', 16)
LINK_MODE = int('0xA000', 16)

block = 0

class SuperBlock(Structure):
    _fields_ = [
        ('magic', c_ulonglong),
        ('version', c_ulonglong),
        ('block_size', c_ulonglong),
        ('structure_info_first_block', c_ulonglong),
        ('structure_info_blocks_count', c_ulonglong),
        ('directory_entries_count', c_ulonglong),
        ('symlinks_count', c_ulonglong),
        ('inodes_count', c_ulonglong)
    ]

# data_offset and count represent different things depending on mode:
# file - number of first block on disk and size in bytes (number of blocks can be deduced)
# directory - index of the first entry in the directory entries array and number of entries
# symlink - index of the entry in the symlink entries array and 1
class Inode(Structure):
    _fields_ = [
        ('mode', c_ulonglong),
        ('inode_no', c_ulonglong), #redundant
        ('data_offset', c_ulonglong),
        ('count', c_ulonglong) # either file size or children count
    ]

# Represents directory entry - file, subdirectory or symlink
# It has a name and i-node number so to know what type of
# entry it is one has to read the i-node
# filename (length: unsigned short followed by characters)
class DirectoryEntry(object):
    def __init__(self,filename,inode_no):
        self.filename = filename
        self.inode_no = inode_no

    def write(self,fp):
        pos = fp.tell()
        fp.write(c_ulonglong(self.inode_no))
        fp.write(c_ushort(len(self.filename)))
        fp.write(bytes(self.filename,'utf-8'))
        return fp.tell() - pos

class SymbolicLink(object):
    def __init__(self,path):
        self.path = path

    def write(self,fp):
        pos = fp.tell()
        fp.write(c_ushort(len(self.path)))
        fp.write(bytes(self.path,'utf-8'))
        return fp.tell() - pos

directory_entries = []
directory_entries_count = 0

symlinks = []
symlinks_count = 0

inodes = []
inodes_count = 1

def next_directory_entry(filename,inode_no):
    global directory_entries
    global directory_entries_count

    directory_entry = DirectoryEntry(filename,inode_no)
    directory_entries_count += 1
    directory_entries.append(directory_entry)

    return directory_entry

def next_symlink(path,directory):
    global symlinks
    global symlinks_count

    symlink = SymbolicLink(path)
    symlinks_count += 1
    symlinks.append(symlink)

    return symlink

def next_inode():
    global inodes_count
    global inodes

    inode = Inode()
    inode.inode_no = inodes_count
    inodes_count += 1
    inodes.append(inode)

    return inode

def pad(fp, size):
    fp.write(b'\0' * size)
    return size

def write_initial_superblock(fp):
    global block
    pad(fp, OSV_BLOCK_SIZE) # superblock is empty at first
    block += 1

def write_file(fp, path):
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

    if total > 0:
        pad(fp, OSV_BLOCK_SIZE - last)

    return total

def write_inodes(fp):
    global inodes

    for inode in inodes:
        fp.write(inode)

    return len(inodes) * sizeof(Inode)

def write_array(fp, vals):
    bytes_written = 0

    for val in vals:
        bytes_written += val.write(fp)

    return bytes_written

def write_dir(fp, manifest, dirpath, parent_dir):
    global directory_entries_count

    directory_entry_inodes = []
    for entry in manifest:
        inode = next_inode()
        directory_entry_inodes.append((entry,inode))

        val = manifest.get(entry)
        if type(val) is dict: # directory
            inode.mode = DIR_MODE
            count, directory_entries_index = write_dir(fp, val, dirpath + '/' + entry, manifest)
            inode.count = count
            inode.data_offset = directory_entries_index
        else: # file or symlink
            if val.startswith('->'): #symlink
                inode.mode = LINK_MODE
                global symlinks_count
                inode.data_offset = symlinks_count
                inode.count = 1
                next_symlink(val[2:],manifest)
                print('Link %s to %s' % (dirpath + '/' + entry, val[2:]))
            else: #file
                inode.mode = REG_MODE
                global block
                inode.data_offset = block
                inode.count = write_file(fp, val)
                print('Adding %s' % (dirpath + '/' + entry))

    # This needs to be added so that later we can walk the tree
    # when fining symlinks
    manifest['.'] = manifest
    manifest['..'] = parent_dir

    this_directory_entries_index = directory_entries_count
    for directory_entry_inode in directory_entry_inodes:
        next_directory_entry(directory_entry_inode[0],directory_entry_inode[1].inode_no)

    this_directory_entries_count = len(directory_entry_inodes)
    return (this_directory_entries_count, this_directory_entries_index)

def write_fs(fp, manifest):
    global block
    global inodes
    global directory_entries
    global symlinks

    root_inode = next_inode()
    root_inode.mode = DIR_MODE

    count, directory_entries_index = write_dir(fp, manifest.get(''), '', manifest)
    root_inode.count = count
    root_inode.data_offset = directory_entries_index

    block_no = block

    # Write directories entries array
    bytes_written = write_array(fp,directory_entries)
    bytes_written += write_array(fp,symlinks)

    # Write inodes!
    write_inodes(fp)
    bytes_written += len(inodes) * sizeof(Inode)

    return (block_no, bytes_written)

def gen_image(out, manifest):
    print('Writing image')
    fp = open(out, 'wb')

    # write the initial superblock
    write_initial_superblock(fp)

    system_structure_block, bytes_written = write_fs(fp, manifest)
    structure_info_last_block_bytes = bytes_written % OSV_BLOCK_SIZE
    structure_info_blocks_count = bytes_written // OSV_BLOCK_SIZE + (1 if structure_info_last_block_bytes > 0 else 0)

    pad(fp,OSV_BLOCK_SIZE - structure_info_last_block_bytes)

    global inodes
    global directory_entries
    global symlinks

    sb = SuperBlock()
    sb.version = 1
    sb.magic = int('0xDEADBEAD', 16)
    sb.block_size = OSV_BLOCK_SIZE
    sb.structure_info_first_block = system_structure_block
    sb.structure_info_blocks_count = structure_info_blocks_count
    sb.directory_entries_count = len(directory_entries)
    sb.symlinks_count = len(symlinks)
    sb.inodes_count = len(inodes)

    print('First block: %d, blocks count: %d' % (sb.structure_info_first_block, sb.structure_info_blocks_count))
    print('Directory entries count %d' % sb.directory_entries_count)
    print('Symlinks count %d' % sb.symlinks_count)
    print('Inodes count %d' % sb.inodes_count)

    fp.seek(0)
    fp.write(sb)

    fp.close()

def parse_manifest(manifest):
    manifest = [(x, y % defines) for (x, y) in manifest]
    files = list(expand(manifest))
    files = [(x, unsymlink(y)) for (x, y) in files]

    file_dict = {}

    def populate_with_directory_path(path,directory):
        tokens = path.rstrip('/').split('/')
        dictionary = directory
        for token in tokens[:-1]:
            dictionary = dictionary.setdefault(token, {})
        return (dictionary,tokens)

    for name, hostname in files:
        p = file_dict
        if hostname.startswith('->'):
            p, tokens = populate_with_directory_path(name,p)
            entry = tokens[len(tokens)-1]
            p[entry] = hostname
        else:
            if hostname.endswith('-stripped.so'):
                continue
            hostname = strip_file(hostname)
            if os.path.islink(hostname):
                p, tokens = populate_with_directory_path(name,p)
                entry = tokens[len(tokens)-1]
                link = os.readlink(hostname)
                p[entry] = '->%s' % link
            elif os.path.isdir(hostname):
                for token in name.rstrip('/').split('/'):
                    p = p.setdefault(token, {})
            else:
                p, tokens = populate_with_directory_path(name,p)
                entry = tokens[len(tokens)-1]
                p[entry] = hostname

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

    manifest = read_manifest(options.manifest)

    outfile = os.path.abspath(options.output)

    manifest = parse_manifest(manifest)

    gen_image(outfile, manifest)

if __name__ == '__main__':
    main()
