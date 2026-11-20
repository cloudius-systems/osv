#!/usr/bin/env python3
#
# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Builds the ext4 images used by tests/misc-ext4-headers.cc: one valid image
# holding /f, and copies that each change one or two header fields.  All but
# rsvgdt-max.img hold a value lwext4 has to refuse at mount or read time.
#
#   good.img        unchanged
#   logblock.img    superblock s_log_block_size = 22 (block size 1024 << 22)
#   ehmax.img       /f's in-inode extent header: eh_max = eh_entries = 0xffff
#   idx0.img        /f's in-inode extent header: eh_depth = 1, eh_entries = 0
#   rsvgdt-max.img  group 0 BLOCK_UNINIT, s_reserved_gdt_blocks at the
#                   largest value the mount check accepts (a quarter block
#                   of descriptor pointers, as Linux and mkfs.ext4)
#   rsvgdt-over-by-one.img  the same plus one
#   rsvgdt-over.img group 0 BLOCK_UNINIT, s_reserved_gdt_blocks so large
#                   that group 0's superblock, GDT and reserved GDT blocks
#                   overflow its block bitmap by one bit (CVE-2025-71474)
#   ipg-over.img    group 0 INODE_UNINIT, s_inodes_per_group one more than
#                   the bits in one block (CVE-2025-71473)
#   gdt-over.img    group 0 BLOCK_UNINIT, 8 blocks per group and a block
#                   count near 2^32: the geometry passes the mount check,
#                   but the 4M descriptor blocks marked in group 0's bitmap
#                   do not fit, so only the bitmap rebuild bound stops it
#
# With BLOCK_UNINIT or INODE_UNINIT set, lwext4 rebuilds a group 0 bitmap
# from the superblock the first time it reads an inode.  The mount check
# refuses rsvgdt-over-by-one, rsvgdt-over and ipg-over before that happens.
#
# Needs mkfs.ext4 and debugfs (e2fsprogs). Usage:
#   scripts/tests/make_ext4_header_images.py OUTDIR

import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

CONTENT = b"lwext4 header test\n"
SB_OFF = 1024                 # primary superblock
SB_LOG_BLOCK_SIZE = 24        # __le32 s_log_block_size
SB_BLOCKS_COUNT = 0x04        # __le32 s_blocks_count_lo
SB_BLOCKS_PER_GROUP = 0x20    # __le32 s_blocks_per_group
SB_CLUSTERS_PER_GROUP = 0x24  # __le32 s_clusters_per_group
SB_INODES_PER_GROUP = 0x28    # __le32 s_inodes_per_group
SB_RESERVED_GDT = 0xCE        # __le16 s_reserved_gdt_blocks
BG0_FLAGS = 4096 + 0x12       # __le16 bg_flags of group 0 (GDT in block 1)
BG_INODE_UNINIT = 0x1
BG_BLOCK_UNINIT = 0x2
BG_ITABLE_ZEROED = 0x4
BLOCK_SIZE = 4096
EXT4_EXTENTS_FL = 0x80000
I_FLAGS = 0x20                # __le32 i_flags in the on-disk inode
I_BLOCK = 0x28                # 60-byte i_block, holds the extent root
EH_MAGIC = 0xF30A


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def inode_offset(img, path):
    """Byte offset of the inode for path, from debugfs."""
    out = run("debugfs", "-R", "imap " + path, img)
    m = re.search(r"located at block (\d+), offset (0x[0-9a-f]+)", out)
    if not m:
        sys.exit("debugfs imap: unexpected output: " + out)
    bsize = int(re.search(r"Block size:\s+(\d+)",
                          run("dumpe2fs", "-h", img)).group(1))
    return int(m.group(1)) * bsize + int(m.group(2), 16)


def patch(img, off, fmt, value):
    with open(img, "r+b") as f:
        f.seek(off)
        f.write(struct.pack(fmt, value))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_ext4_header_images.py OUTDIR")
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    good = os.path.join(out, "good.img")

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "f")
        with open(src, "wb") as f:
            f.write(CONTENT)
        if os.path.exists(good):
            os.remove(good)
        # 4 KiB blocks and no journal (the OSv lwext4 build disables it).
        # No metadata_csum either: with checksums, every edited copy would
        # be refused by a checksum mismatch before the field under test is
        # ever read (and metadata_csum_seed, a newer mkfs default, is not
        # supported by lwext4 at all). /f is written by debugfs, so no host
        # mount is needed.
        run("mkfs.ext4", "-q", "-F", "-b", "4096",
            "-O", "^has_journal,^metadata_csum,^metadata_csum_seed",
            good, "16M")
        run("debugfs", "-w", "-R", "write " + src + " f", good)

    ino = inode_offset(good, "/f")
    with open(good, "rb") as f:
        f.seek(ino + I_FLAGS)
        flags, = struct.unpack("<I", f.read(4))
        f.seek(ino + I_BLOCK)
        magic, entries, emax, depth = struct.unpack("<HHHH", f.read(8))
    if not flags & EXT4_EXTENTS_FL or magic != EH_MAGIC:
        sys.exit("/f does not use an in-inode extent tree")
    if (entries, emax, depth) != (1, 4, 0):
        sys.exit("unexpected root extent header %r" % ((entries, emax, depth),))

    def variant(name):
        path = os.path.join(out, name)
        shutil.copyfile(good, path)
        return path

    patch(variant("logblock.img"), SB_OFF + SB_LOG_BLOCK_SIZE, "<I", 22)
    eh = variant("ehmax.img")
    patch(eh, ino + I_BLOCK + 2, "<H", 0xffff)   # eh_entries
    patch(eh, ino + I_BLOCK + 4, "<H", 0xffff)   # eh_max
    idx = variant("idx0.img")
    patch(idx, ino + I_BLOCK + 2, "<H", 0)       # eh_entries
    patch(idx, ino + I_BLOCK + 6, "<H", 1)       # eh_depth

    # Group 0 holds the superblock, one GDT block (the image has a single
    # group) and the reserved GDT blocks, so lwext4 marks 2 + reserved bits
    # before anything else.  ITABLE_ZEROED keeps it from zeroing the inode
    # table.
    with open(good, "rb") as f:
        f.seek(SB_OFF + SB_BLOCKS_COUNT)
        blocks, = struct.unpack("<I", f.read(4))
        f.seek(SB_OFF + SB_BLOCKS_PER_GROUP)
        bpg, = struct.unpack("<I", f.read(4))
    if blocks > bpg:
        sys.exit("expected a single block group, got %d blocks of %d per "
                 "group" % (blocks, bpg))
    bitmap_bits = BLOCK_SIZE * 8
    for name, reserved in (("rsvgdt-max.img", BLOCK_SIZE // 4),
                           ("rsvgdt-over-by-one.img", BLOCK_SIZE // 4 + 1),
                           ("rsvgdt-over.img", bitmap_bits - 1)):
        rg = variant(name)
        patch(rg, SB_OFF + SB_RESERVED_GDT, "<H", reserved)
        patch(rg, BG0_FLAGS, "<H", BG_BLOCK_UNINIT | BG_ITABLE_ZEROED)
    gdt = variant("gdt-over.img")
    patch(gdt, SB_OFF + SB_BLOCKS_COUNT, "<I", 0xfffffff8)
    patch(gdt, SB_OFF + SB_BLOCKS_PER_GROUP, "<I", 8)
    patch(gdt, SB_OFF + SB_CLUSTERS_PER_GROUP, "<I", 8)
    patch(gdt, BG0_FLAGS, "<H", BG_BLOCK_UNINIT | BG_ITABLE_ZEROED)
    ipg = variant("ipg-over.img")
    patch(ipg, SB_OFF + SB_INODES_PER_GROUP, "<I", bitmap_bits + 1)
    patch(ipg, BG0_FLAGS, "<H", BG_INODE_UNINIT | BG_ITABLE_ZEROED)
    print("inode /f at byte %d; images in %s" % (ino, out))


if __name__ == "__main__":
    main()
