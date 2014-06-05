#!/usr/bin/python2

import gdb
import re
import os, os.path
import struct
import json
import math
import itertools
import operator
import heapq
from glob import glob
from collections import defaultdict

arch = 'x64'
build_dir = os.path.dirname(gdb.current_objfile().filename)
osv_dir = os.path.abspath(os.path.join(build_dir, '../..'))
mgmt_dir = os.path.join(osv_dir, 'mgmt')
external = os.path.join(osv_dir, 'external', arch)

sys.path.append(os.path.join(osv_dir, 'scripts'))

from osv.trace import Trace,TracePoint,BacktraceFormatter,format_time,format_duration
from osv import trace, debug

virtio_driver_type = gdb.lookup_type('virtio::virtio_driver')

class status_enum_class(object):
    pass
status_enum = status_enum_class()

phys_mem = 0xffffc00000000000

def pt_index(addr, level):
    return (addr >> (12 + 9 * level)) & 511

def phys_cast(addr, type):
    return gdb.parse_and_eval('0x%x' % (addr + phys_mem)).cast(type.pointer())

def values(_dict):
    if hasattr(_dict, 'viewvalues'):
        return _dict.viewvalues()
    return _dict.values()

def read_vector(v):
    impl = v['_M_impl']
    ptr = impl['_M_start']
    end = impl['_M_finish']
    while ptr != end:
        yield ptr.dereference()
        ptr += 1

def load_elf(path, base):
    args = ''
    text_addr = '?'
    unwanted_sections = ['.text',
                         '.note.stapsdt',
                         '.gnu_debuglink',
                         '.gnu_debugdata',
                         '.shstrtab',
                         ]
    for line in os.popen('readelf -WS ' + path):
        m = re.match(r'\s*\[ *\d+\]\s+([\.\w\d_]+)\s+\w+\s+([0-9a-f]+).*', line)
        if m:
            section = m.group(1)
            if section == 'NULL':
                continue
            addr = hex(int(m.group(2), 16) + base)
            if section == '.text':
                text_addr = addr
            if section not in unwanted_sections:
                args += ' -s %s %s' % (section, addr)

    gdb.execute('add-symbol-file %s %s %s' % (path, text_addr, args))

class syminfo_resolver(object):
    cache = dict()

    def __call__(self, addr):
        if addr in syminfo_resolver.cache:
            return self.cache[addr]
        infosym = gdb.execute('info symbol 0x%x' % addr, False, True)
        func = infosym[:infosym.find(" + ")]
        sal = gdb.find_pc_line(addr)
        filename = None
        line = None
        try :
            # prefer (filename:line),
            filename = sal.symtab.filename
            line = sal.line
        except :
            # but if can't get it, at least give the name of the object
            if not infosym.startswith("No symbol matches") :
                filename = infosym[infosym.rfind("/")+1:].rstrip()

        if filename and filename.startswith('../../'):
            filename = filename[6:]
        result = [debug.SourceAddress(addr, name=func, filename=filename, line=line)]
        syminfo_resolver.cache[addr] = result
        return result

    @classmethod
    def clear_cache(clazz):
        clazz.cache.clear()

symbol_resolver = syminfo_resolver()

def symbol_formatter(src_addr):
    ret = src_addr.name
    if src_addr.filename or src_addr.line:
        ret += ' ('
        ret += src_addr.filename
        if src_addr.line:
            ret += ':' + str(src_addr.line)
        ret += ')'
    return ret

def syminfo(addr):
    return symbol_formatter(symbol_resolver(addr)[0])

def translate(path):
    '''given a path, try to find it on the host OS'''
    name = os.path.basename(path)
    for top in [build_dir, mgmt_dir, external, '/zfs']:
        for root, dirs, files in os.walk(top):
            if name in files:
                return os.path.join(root, name)
    return None

class Connect(gdb.Command):
    '''Connect to a local kvm instance at port :1234'''
    def __init__(self):
        gdb.Command.__init__(self,
                             'connect',
                             gdb.COMMAND_NONE,
                             gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        gdb.execute('target remote :1234')
        global status_enum
        status_enum.running = gdb.parse_and_eval('sched::thread::running')
        status_enum.waiting = gdb.parse_and_eval('sched::thread::waiting')
        status_enum.queued = gdb.parse_and_eval('sched::thread::queued')
        status_enum.waking = gdb.parse_and_eval('sched::thread::waking')
        

Connect()


class LogTrace(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self,
                             'logtrace',
                             gdb.COMMAND_NONE,
                             gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        gdb.execute('shell rm -f gdb.txt')
        gdb.execute('set pagination off')
        gdb.execute('set logging on')
        gdb.execute('osv trace')
        gdb.execute('set logging off')

LogTrace()

#
# free_page_ranges generator, use pattern:
# for range in free_page_ranges():
#     pass
#
def free_page_ranges(node = None):
    if (node == None):
        fpr = gdb.lookup_global_symbol('memory::free_page_ranges').value()
        p = fpr['tree_']['data_']['node_plus_pred_']
        node = p['header_plus_size_']['header_']['parent_']
    
    if node:
        page_range = node.cast(gdb.lookup_type('void').pointer()) - 8
        page_range = page_range.cast(gdb.lookup_type('memory::page_range').pointer())
        
        for x in free_page_ranges(node['left_']):
            yield x
            
        yield page_range
        
        for x in free_page_ranges(node['right_']):
            yield x

def vma_list(node = None):
    if (node == None):
        fpr = gdb.lookup_global_symbol('mmu::vma_list').value()
        p = fpr['tree_']['data_']['node_plus_pred_']
        node = p['header_plus_size_']['header_']['parent_']

    if node:
        offset = gdb.parse_and_eval("(int)&(('mmu::vma'*)0)->_vma_list_hook");
        vma = node.cast(gdb.lookup_type('void').pointer()) - offset
        vma = vma.cast(gdb.lookup_type('mmu::vma').pointer())

        for x in vma_list(node['left_']):
            yield x

        yield vma

        for x in vma_list(node['right_']):
            yield x

class osv(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv',
                             gdb.COMMAND_USER, gdb.COMPLETE_COMMAND, True)

class osv_heap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv heap',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        for page_range in free_page_ranges():
            print('%s 0x%016x' % (page_range, page_range['size']))

class osv_memory(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv memory',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        freemem = 0
        for page_range in free_page_ranges():
            freemem += int(page_range['size'])

        mmapmem = 0
        for vma in vma_list():
            start = ulong(vma['_range']['_start'])
            end   = ulong(vma['_range']['_end'])
            size  = ulong(end - start)
            mmapmem += size
            
        memsize = gdb.parse_and_eval('memory::phys_mem_size')
        
        print ("Total Memory: %d Bytes" % memsize)
        print ("Mmap Memory:  %d Bytes (%.2f%%)" %
               (mmapmem, (mmapmem*100.0/memsize)))
        print ("Free Memory:  %d Bytes (%.2f%%)" % 
               (freemem, (freemem*100.0/memsize)))

class osv_waiters(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv waiters',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        reclaimer = gdb.lookup_global_symbol("memory::reclaimer_thread")
        waiters = reclaimer.value()["_oom_blocked"]["_waiters"]
        waiters_list = intrusive_list(waiters)
        gdb.write('waiters:\n')
        for w in waiters_list:
            t = w["owner"].dereference().cast(thread_type)["_id"]
            print(t)
            gdb.write("Thread %d waits for %d Bytes\n" % (t, int(w["bytes"])))

#
# Returns a u64 value from a stats given a field name.
#
def get_stat_by_name(stats, stats_cast, field):
    return int(gdb.parse_and_eval('('+str(stats_cast)+' '+str(stats)+')->'+str(field)+'.value.ui64'))

class osv_zfs(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv zfs',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        zil_replay_disable = gdb.parse_and_eval('zil_replay_disable')
        zfs_nocacheflush = gdb.parse_and_eval('zfs_nocacheflush')
        # file-level prefetch feature
        zfs_prefetch_disable = gdb.parse_and_eval('zfs_prefetch_disable')
        zfs_no_write_throttle = gdb.parse_and_eval('zfs_no_write_throttle')
        zfs_txg_timeout = gdb.parse_and_eval('zfs_txg_timeout')
        zfs_write_limit_override = gdb.parse_and_eval('zfs_write_limit_override')
        # Min/Max number of concurrent pending I/O requests on each device
        vdev_min_pending = gdb.parse_and_eval('zfs_vdev_min_pending')
        vdev_max_pending = gdb.parse_and_eval('zfs_vdev_max_pending')

        print (":: ZFS TUNABLES ::")
        print ("\tzil_replay_disable:       %d" % zil_replay_disable)
        print ("\tzfs_nocacheflush:         %d" % zfs_nocacheflush)
        print ("\tzfs_prefetch_disable:     %d" % zfs_prefetch_disable)
        print ("\tzfs_no_write_throttle:    %d" % zfs_no_write_throttle)
        print ("\tzfs_txg_timeout:          %d" % zfs_txg_timeout)
        print ("\tzfs_write_limit_override: %d" % zfs_write_limit_override)
        print ("\tvdev_min_pending:         %d" % vdev_min_pending)
        print ("\tvdev_max_pending:         %d" % vdev_max_pending)

        # virtual device read-ahead cache details (device-level prefetch)
        vdev_cache_size = gdb.parse_and_eval('zfs_vdev_cache_size')
        if vdev_cache_size != 0:
            # Vdev cache size (virtual device read-ahead cache; design: LRU read-ahead cache)
            # I/O smaller than vdev_cache_max will be turned into (1 << vdev_cache_bshift)
            vdev_cache_max = gdb.parse_and_eval('zfs_vdev_cache_max')
            # Shift to inflate low size I/O request
            vdev_cache_bshift = gdb.parse_and_eval('zfs_vdev_cache_bshift')

            print (":: VDEV SETTINGS ::")
            print ("\tvdev_cache_max:    %d" % vdev_cache_max)
            print ("\tvdev_cache_size:   %d" % vdev_cache_size)
            print ("\tvdev_cache_bshift: %d" % vdev_cache_bshift)

            # Get address of 'struct vdc_stats vdc_stats'
            vdc_stats_struct = int(gdb.parse_and_eval('(u64) &vdc_stats'))
            vdc_stats_cast = '(struct vdc_stats *)'

            vdev_delegations = get_stat_by_name(vdc_stats_struct, vdc_stats_cast, 'vdc_stat_delegations')
            vdev_hits = get_stat_by_name(vdc_stats_struct, vdc_stats_cast, 'vdc_stat_hits')
            vdev_misses = get_stat_by_name(vdc_stats_struct, vdc_stats_cast, 'vdc_stat_misses')

            print ("\t\tvdev_cache_delegations:  %d" % vdev_delegations)
            print ("\t\tvdev_cache_hits:         %d" % vdev_hits)
            print ("\t\tvdev_cache_misses:       %d" % vdev_misses)

        # Get address of 'struct arc_stats arc_stats'
        arc_stats_struct = int(gdb.parse_and_eval('(u64) &arc_stats'))
        arc_stats_cast = '(struct arc_stats *)'

        arc_size = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_size')
        arc_target_size = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_c')
        arc_min_size = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_c_min')
        arc_max_size = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_c_max')

        # Cache size breakdown
        arc_mru_size = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_p')
        arc_mfu_size = arc_target_size - arc_mru_size
        arc_mru_perc = 100 * (float(arc_mru_size) / arc_target_size)
        arc_mfu_perc = 100 * (float(arc_mfu_size) / arc_target_size)

        print (":: ARC SIZES ::")
        print ("\tActual ARC Size:        %d" % arc_size)
        print ("\tTarget size of ARC:     %d" % arc_target_size)
        print ("\tMin Target size of ARC: %d" % arc_min_size)
        print ("\tMax Target size of ARC: %d" % arc_max_size)
        print ("\t\tMost Recently Used (MRU) size:   %d (%.2f%%)" %
               (arc_mru_size, arc_mru_perc))
        print ("\t\tMost Frequently Used (MFU) size: %d (%.2f%%)" %
               (arc_mfu_size, arc_mfu_perc))

        # Cache hits/misses
        arc_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_hits')
        arc_misses = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_misses')
        arc_ac_total = arc_hits + arc_misses;
        arc_hits_perc = 100 * (float(arc_hits) / arc_ac_total);
        arc_misses_perc = 100 * (float(arc_misses) / arc_ac_total);

        print (":: ARC EFFICIENCY ::")
        print ("Total ARC accesses: %d" % arc_ac_total)
        print ("\tARC hits: %d (%.2f%%)" %
               (arc_hits, arc_hits_perc))

        arc_mru_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_mru_hits')
        arc_mru_ghost_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_mru_ghost_hits')
        arc_mru_hits_perc = 100 * (float(arc_mru_hits) / arc_hits);

        print ("\t\tARC MRU hits: %d (%.2f%%)" %
               (arc_mru_hits, arc_mru_hits_perc))
        print ("\t\t\tGhost Hits: %d" % arc_mru_ghost_hits)

        arc_mfu_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_mfu_hits')
        arc_mfu_ghost_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_mfu_ghost_hits')
        arc_mfu_hits_perc = 100 * (float(arc_mfu_hits) / arc_hits);

        print ("\t\tARC MFU hits: %d (%.2f%%)" %
               (arc_mfu_hits, arc_mfu_hits_perc))
        print ("\t\t\tGhost Hits: %d" % arc_mfu_ghost_hits)

        print ("\tARC misses: %d (%.2f%%)" %
               (arc_misses, arc_misses_perc))

        # Streaming ratio
        prefetch_data_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_prefetch_data_hits')
        prefetch_data_misses = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_prefetch_data_misses')
        prefetch_metadata_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_prefetch_metadata_hits')
        prefetch_metadata_misses = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_prefetch_metadata_misses')
        prefetch_total = float(prefetch_data_hits + prefetch_data_misses + prefetch_metadata_hits + prefetch_metadata_misses)

        print ("Prefetch workload ratio: %.4f%%" % (prefetch_total / arc_ac_total))
        print ("Prefetch total:          %d" % prefetch_total)
        print ("\tPrefetch hits:   %d" % (prefetch_data_hits + prefetch_metadata_hits))
        print ("\tPrefetch misses: %d" % (prefetch_data_misses + prefetch_metadata_misses))

        # Hash data
        arc_hash_elements = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_hash_elements')
        arc_max_hash_elements = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_hash_elements_max')
        arc_hash_collisions = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_hash_collisions')
        arc_hash_chains = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_hash_chains')

        print ("Total Hash elements: %d" % arc_hash_elements)
        print ("\tMax Hash elements: %d" % arc_max_hash_elements)
        print ("\tHash collisions:   %d" % arc_hash_collisions)
        print ("\tHash chains:       %d" % arc_hash_chains)

        # L2ARC (not displayed if not supported)
        l2arc_size = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_l2_size')

        if l2arc_size != 0:
            l2arc_hits = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_l2_hits')
            l2arc_misses = get_stat_by_name(arc_stats_struct, arc_stats_cast, 'arcstat_l2_misses')
            l2arc_ac_total = l2arc_hits + l2arc_misses;
            l2arc_hits_perc = 100 * (float(l2arc_hits) / l2arc_ac_total);
            l2arc_misses_perc = 100 * (float(l2arc_misses) / l2arc_ac_total);

            print (":: L2ARC ::")
            print ("\tActual L2ARC Size: %d" % l2arc_size)
            print ("Total L2ARC accesses: %d" % l2arc_ac_total)
            print ("\tL2ARC hits:   %d (%.2f%%)" %
                   (l2arc_hits, l2arc_hits_perc))
            print ("\tL2ARC misses: %d (%.2f%%)" %
                   (l2arc_misses, l2arc_misses_perc))

def bits2str(bits, chars):
    r = ''
    if bits == 0:
        return 'none'.ljust(len(chars))
    for i in range(len(chars)):
        if bits & (1 << i):
            r += chars[i]
    return r.ljust(len(chars))

def permstr(perm):
    return bits2str(perm, ['r', 'w', 'x'])

def flagstr(flags):
    return bits2str(flags, ['f', 'p', 's', 'u', 'j', 'm', 'b'])

class osv_mmap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv mmap',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        for vma in vma_list():
            start = ulong(vma['_range']['_start'])
            end   = ulong(vma['_range']['_end'])
            flags =  flagstr(ulong(vma['_flags']))
            perm =  permstr(ulong(vma['_perm']))
            size  = '{:<16}'.format('[%s kB]' % (ulong(end - start)/1024))
            print('0x%016x 0x%016x %s flags=%s perm=%s' % (start, end, size, flags, perm))
    
ulong_type = gdb.lookup_type('unsigned long')
timer_type = gdb.lookup_type('sched::timer_base')
thread_type = gdb.lookup_type('sched::thread')

active_thread_context = None

def ulong(x):
    if isinstance(x, gdb.Value):
        x = x.cast(ulong_type)
    x = int(x)
    if x < 0:
        x += 1 << 64
    return x

def to_int(gdb_value):
    if hasattr(globals()['__builtins__'], 'long'):
        # For GDB with python2
        return long(gdb_value)
    return int(gdb_value)

class osv_syms(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv syms',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        syminfo_resolver.clear_cache()
        p = gdb.lookup_global_symbol('elf::program::s_objs').value()
        p = p.dereference().address
        while p.dereference():
            obj = p.dereference().dereference()
            base = to_int(obj['_base'])
            obj_path = obj['_pathname']['_M_dataplus']['_M_p'].string()
            path = translate(obj_path)
            if not path:
                print('ERROR: Unable to locate object file for:', obj_path, hex(base))
            else:
                print(path, hex(base))
                load_elf(path, base)
            p += 1

class osv_info(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class cpu(object):
    def __init__(self, cpu_thread):
        self.load(cpu_thread)
    def load(self, cpu_thread):
        self.cpu_thread = cpu_thread
        self.id = cpu_thread.num - 1
        cur = gdb.selected_thread()
        try:
            self.cpu_thread.switch()
            old = gdb.selected_frame()
            try:
                gdb.newest_frame().select()
                self.rsp = ulong(gdb.parse_and_eval('$rsp'))
                self.rbp = ulong(gdb.parse_and_eval('$rbp'))
                self.rip = ulong(gdb.parse_and_eval('$rip'))
            finally:
                old.select()
        finally:
            cur.switch()
        g_cpus = gdb.parse_and_eval('sched::cpus._M_impl._M_start')
        self.obj = g_cpus + self.id

def template_arguments(gdb_type):
    n = 0;
    while True:
        try:
            yield gdb_type.template_argument(n)
            n += 1
        except RuntimeError:
            return

def get_template_arg_with_prefix(gdb_type, prefix):
    for arg in template_arguments(gdb_type):
        if str(arg).startswith(prefix):
            return arg

def get_base_class_offset(gdb_type, base_class_name):
    name_pattern = re.escape(base_class_name) + "(<.*>)?$"
    for field in gdb_type.fields():
        if field.is_base_class and re.match(name_pattern, field.name):
            return field.bitpos / 8

def derived_from(type, base_class):
    return len([x for x in type.fields()
                if x.is_base_class and x.type == base_class]) != 0

class unordered_map:

    def __init__(self, map_ref):
        map_header = map_ref['_M_h']
        map_type = map_header.type.strip_typedefs()
        self.node_type = gdb.lookup_type(str(map_type) +  '::__node_type').pointer()
        self.begin = map_header['_M_bbegin']

    def __iter__(self):
        begin = self.begin
        while begin:
            node = begin.cast(self.node_type).dereference()
            elem = node["_M_v"]
            yield elem["second"]
            begin = node["_M_nxt"]

class intrusive_list:
    size_t = gdb.lookup_type('size_t')

    def __init__(self, list_ref):
        list_type = list_ref.type.strip_typedefs()
        self.node_type = list_type.template_argument(0)
        self.root = list_ref['data_']['root_plus_size_']['root_']

        member_hook = get_template_arg_with_prefix(list_type, "boost::intrusive::member_hook")
        if member_hook:
            self.link_offset = member_hook.template_argument(2).cast(self.size_t)
        else:
            self.link_offset = get_base_class_offset(self.node_type, "boost::intrusive::list_base_hook")
            if self.link_offset == None:
                raise Exception("Class does not extend list_base_hook: " + str(self.node_type))

    def __iter__(self):
        hook = self.root['next_']
        while hook != self.root.address:
            node_ptr = hook.cast(self.size_t) - self.link_offset
            yield node_ptr.cast(self.node_type.pointer()).dereference()
            hook = hook['next_']

    def __nonzero__(self):
        return self.root['next_'] != self.root.address

    def __bool__(self):
        return self.__nonzero__()

class vmstate(object):
    def __init__(self):
        self.reload()

    def reload(self):
        self.load_cpu_list()
        self.load_thread_list()

    def load_cpu_list(self):
        # cause gdb to initialize thread list
        gdb.execute('info threads', False, True)
        cpu_list = {}
        for cpu_thread in gdb.selected_inferior().threads():
            c = cpu(cpu_thread)
            cpu_list[c.id] = c
        self.cpu_list = cpu_list

    def load_thread_list(self):
        threads = map(gdb.Value.dereference, unordered_map(gdb.lookup_global_symbol('sched::thread_map').value()))
        self.thread_list = sorted(threads, key=lambda x: int(x["_id"]))

    def cpu_from_thread(self, thread):
        stack = thread['_attr']['_stack']
        stack_begin = ulong(stack['begin'])
        stack_size = ulong(stack['size'])
        for c in values(self.cpu_list):
            if c.rsp > stack_begin and c.rsp <= stack_begin + stack_size:
                return c
        return None

class thread_context(object):
    def __init__(self, thread, state):
        self.old_frame = gdb.selected_frame()
        self.new_frame = gdb.newest_frame()
        self.new_frame.select()
        self.old_rsp = gdb.parse_and_eval('$rsp').cast(ulong_type)
        self.old_rip = gdb.parse_and_eval('$rip').cast(ulong_type)
        self.old_rbp = gdb.parse_and_eval('$rbp').cast(ulong_type)
        self.running_cpu = state.cpu_from_thread(thread)
        self.vm_thread = gdb.selected_thread()
        if not self.running_cpu:
            self.old_frame.select()
            self.new_rsp = thread['_state']['rsp'].cast(ulong_type)
            self.new_rip = thread['_state']['rip'].cast(ulong_type)
            self.new_rbp = thread['_state']['rbp'].cast(ulong_type)
    def __enter__(self):
        self.new_frame.select()
        if not self.running_cpu:
            gdb.execute('set $rsp = %s' % self.new_rsp)
            gdb.execute('set $rip = %s' % self.new_rip)
            gdb.execute('set $rbp = %s' % self.new_rbp)
        else:
            self.running_cpu.cpu_thread.switch()
    def __exit__(self, *_):
        if not self.running_cpu:
            gdb.execute('set $rsp = %s' % self.old_rsp)
            gdb.execute('set $rip = %s' % self.old_rip)
            gdb.execute('set $rbp = %s' % self.old_rbp)
        else:
            self.vm_thread.switch()
        self.old_frame.select()

def exit_thread_context():
    global active_thread_context
    if active_thread_context:
        active_thread_context.__exit__()
        active_thread_context = None

timer_state_expired = gdb.parse_and_eval('sched::timer_base::expired')

def show_thread_timers(t):
    timer_list = intrusive_list(t['_active_timers'])
    if timer_list:
        gdb.write('  timers:')
        for timer in timer_list:
            expired = '*' if timer['_state'] == timer_state_expired else ''
            expiration = int(timer['_time']['__d']['__r']) / 1.0e9
            gdb.write(' %11.9f%s' % (expiration, expired))
        gdb.write('\n')

def get_function_name(frame):
    if frame.function():
        return frame.function().name
    else:
        return '??'

class ResolvedFrame:
    def __init__(self, frame, file_name, line, func_name):
        self.frame = frame
        self.file_name = file_name
        self.line = line
        self.func_name = func_name

def traverse_resolved_frames(frame):
    while frame:
        if frame.type() == gdb.INLINE_FRAME:
            frame = frame.older()
            continue

        sal = frame.find_sal()
        if not sal:
            return

        symtab = sal.symtab
        if not symtab:
            return

        func_name = get_function_name(frame)
        if not func_name:
            return

        yield ResolvedFrame(frame, symtab.filename, sal.line, func_name)
        frame = frame.older()

def strip_dotdot(path):
    if path[:6] == "../../":
           return path[6:]
    return path

def find_or_give_last(predicate, seq):
    last = None
    for element in seq:
        if predicate(element):
            return element
        last = element
    return last

def unique_ptr_get(u):
    return u['_M_t']['_M_head_impl']

def thread_cpu(t):
    d = unique_ptr_get(t['_detached_state'])
    return d['_cpu'];

def thread_status(t):
    d = unique_ptr_get(t['_detached_state'])
    return str(d['st']['_M_i']).replace('sched::thread::', '')

class osv_info_threads(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info threads',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, for_tty):
        thread_nr = 0
        exit_thread_context()
        state = vmstate()
        for t in state.thread_list:
            with thread_context(t, state):
                cpu = thread_cpu(t)
                tid = t['_id']
                name = t['_attr']['_name']['_M_elems'].string()
                newest_frame = gdb.selected_frame()
                # Non-running threads have always, by definition, just called
                # a reschedule, and the stack trace is filled with reschedule
                # related functions (switch_to, schedule, wait_until, etc.).
                # Here we try to skip such functions and instead show a more
                # interesting caller which initiated the wait.
                file_blacklist = ["arch-switch.hh", "sched.cc", "sched.hh",
                                  "mutex.hh", "mutex.cc", "mutex.c", "mutex.h"]

                # Functions from blacklisted files which are interesting
                sched_thread_join = 'sched::thread::join()'
                function_whitelist = [sched_thread_join]

                def is_interesting(resolved_frame):
                    is_whitelisted = resolved_frame.func_name in function_whitelist
                    is_blacklisted = os.path.basename(resolved_frame.file_name) in file_blacklist
                    return is_whitelisted or not is_blacklisted

                fr = find_or_give_last(is_interesting, traverse_resolved_frames(newest_frame))

                if fr:
                    location = '%s at %s:%s' % (fr.func_name, strip_dotdot(fr.file_name), fr.line)
                else:
                    location = '??'

                gdb.write('%4d (0x%x) %-15s cpu%s %-10s %s vruntime %12g\n' %
                          (tid, ulong(t.address), name,
                           cpu['arch']['acpi_id'] if cpu != 0 else "?",
                           thread_status(t),
                           location,
                           t['_runtime']['_Rtt'],
                           )
                          )

                if fr and fr.func_name == sched_thread_join:
                    gdb.write("\tjoining on %s\n" % fr.frame.read_var("this"))

                show_thread_timers(t)
                thread_nr += 1
        gdb.write('Number of threads: %d\n' % thread_nr)

class osv_info_callouts(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info callouts',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, for_tty):
        c = str(gdb.lookup_global_symbol('callouts::_callouts').value())
        callouts = re.findall('\[([0-9]+)\] = (0x[0-9a-zA-Z]+)', c)
        
        gdb.write("%-5s%-40s%-40s%-30s%-10s\n" % ("id", "addr", "function", "abs time (ns)", "flags"))
        
        # We have a valid callout frame
        for desc in callouts:
            id = int(desc[0])
            addr = desc[1]
            callout = gdb.parse_and_eval('(struct callout *)' + addr)
            fname = callout['c_fn']
            
            # time
            t = int(callout['c_to_ns'])
            
            # flags
            CALLOUT_ACTIVE = 0x0002
            CALLOUT_PENDING = 0x0004
            CALLOUT_COMPLETED = 0x0020
            f = int(callout['c_flags'])
            
            flags = ("0x%04x " % f) + \
                    ("A" if (callout['c_flags'] & CALLOUT_ACTIVE) else "") + \
                    ("P" if (callout['c_flags'] & CALLOUT_PENDING) else "") + \
                    ("C" if (callout['c_flags'] & CALLOUT_COMPLETED) else "")
            
            # dispatch time ns  ticks callout function
            gdb.write("%-5d%-40s%-40s%-30s%-10s\n" %
                      (id, callout, fname, t, flags))
                
class osv_thread(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)
    def invoke(self, arg, for_tty):
        exit_thread_context()
        state = vmstate()
        thread = None
        for t in state.thread_list:
            if ulong(t.address) == int(arg, 0):
                thread = t
            with thread_context(t, state):
                if to_int(t['_id']) == int(arg, 0):
                    thread = t
        if not thread:
            print('Not found')
            return
        active_thread_context = thread_context(thread, state)
        active_thread_context.__enter__()

class osv_thread_apply(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread apply', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class osv_thread_apply_all(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv thread apply all', gdb.COMMAND_USER,
                             gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        exit_thread_context()
        state = vmstate()
        for t in state.thread_list:
            gdb.write('thread %s %s\n\n' % (t.address, t["_attr"]["_name"]["_M_elems"].string().strip('\0')))
            with thread_context(t, state):
                gdb.execute(arg, from_tty)
            gdb.write('\n')

def continue_handler(event):
    exit_thread_context()

gdb.events.cont.connect(continue_handler)

def setup_libstdcxx():
    gcc = external + '/gcc.bin'
    sys.path += [gcc + '/usr/share/gdb/auto-load/usr/lib64',
                 glob(gcc + '/usr/share/gcc-*/python')[0],
                 ]
    main = glob(gcc + '/usr/share/gdb/auto-load/usr/lib64/libstdc++.so.*.py')[0]
    exec(compile(open(main).read(), main, 'exec'))

def sig_to_string(sig):
    '''Convert a tracepoing signature to a string'''
    return sig.replace('p', '50p')

def align_down(v, pagesize):
    return v & ~(pagesize - 1)

def align_up(v, pagesize):
    return align_down(v + pagesize - 1, pagesize)

class concat(object):
    def __init__(self, view1, view2):
        self.view1 = view1
        self.view2 = view2

    def __getitem__(self, index):
        if isinstance(index, slice):
            l  = len(self.view1)
            if index.start >= l:
                return self.view2.__getitem__(slice(index.start - l, index.stop - l, index.step))
            if index.stop > l:
                raise Exception('Slice spans view boundary, index.start=%d, index.stop=%d, l=%d'
                    % (index.start, index.stop, l))
            return self.view1.__getitem__(index)

        if index < len(self.view1):
            return self.view1[index]
        return self.view2[index - len(self.view1)]

    def __len__(self):
        return len(self.view1) + len(self.view2)

def all_traces():
    # XXX: needed for GDB to see 'trace_page_size'
    gdb.lookup_global_symbol('gdb_trace_function_entry')

    inf = gdb.selected_inferior()
    trace_page_size = ulong(gdb.parse_and_eval('trace_page_size'))
    tp_ptr = gdb.lookup_type('tracepoint_base').pointer()
    backtrace_len = ulong(gdb.parse_and_eval('tracepoint_base::backtrace_len'))
    tracepoints = {}

    state = vmstate();

    trace_buffer_offset = ulong(gdb.parse_and_eval('&percpu_trace_buffer._var'))

    def one_cpu_trace(cpu):
        precpu_base = ulong(cpu.obj['percpu_base'])
        trace_buffer = gdb.parse_and_eval('(trace_buf *)0x%x' % (precpu_base + trace_buffer_offset))
        trace_log_base_ptr = trace_buffer['_base']
        trace_log_base  = unique_ptr_get(trace_log_base_ptr)
        last = ulong(trace_buffer['_last'])
        max_trace = ulong(trace_buffer['_size'])

        if not trace_log_base:
            raise StopIteration

        trace_log = inf.read_memory(trace_log_base, max_trace)

        last %= max_trace
        pivot = align_up(last, trace_page_size)
        trace_log = concat(trace_log[pivot:], trace_log[:last])

        unpacker = trace.SlidingUnpacker(trace_log)
        while unpacker:
            tp_key, = unpacker.unpack('Q')
            if tp_key == 0:
                unpacker.align_up(trace_page_size)
                continue

            # end marker. record being written
            if tp_key == -1:
                break

            thread, thread_name, time, cpu, flags = unpacker.unpack('Q16sQII')
            thread_name = thread_name.partition(b'\0')[0].decode()

            tp = tracepoints.get(tp_key, None)
            if not tp:
                tp_ref = gdb.Value(tp_key).cast(tp_ptr)

                tp = TracePoint(tp_key, str(tp_ref["name"].string()),
                    sig_to_string(str(tp_ref["sig"].string())), str(tp_ref["format"].string()))
                tracepoints[tp_key] = tp

            backtrace = None
            if flags & 1:
                backtrace = unpacker.unpack('Q' * backtrace_len)

            data = unpacker.unpack(tp.signature)
            unpacker.align_up(8)
            yield Trace(tp, thread, thread_name, time, cpu, data, backtrace=backtrace)

    iters = map(lambda cpu: one_cpu_trace(cpu), values(state.cpu_list))
    return heapq.merge(*iters)

def save_traces_to_file(filename):
    trace.write_to_file(filename, list(all_traces()))

def make_symbolic(addr):
    return str(syminfo(addr))

def dump_trace(out_func):
    indents = defaultdict(int)
    bt_formatter = BacktraceFormatter(symbol_resolver, symbol_formatter)

    def lookup_tp(name):
        return gdb.lookup_global_symbol(name).value().dereference()
    tp_fn_entry = lookup_tp('gdb_trace_function_entry')
    tp_fn_exit = lookup_tp('gdb_trace_function_exit')

    for trace in all_traces():
        thread = trace.thread
        time = trace.time
        cpu = trace.cpu
        tp = trace.tp

        def trace_function(indent, annotation, data):
            fn, caller = data
            try:
                block = gdb.block_for_pc(to_int(fn))
                fn_name = block.function.print_name
            except:
                fn_name = '???'
            out_func('0x%016x %2d %19s %s %s %s\n'
                      % (thread,
                         cpu,
                         format_time(time),
                         indent,
                         annotation,
                         fn_name,
                         ))

        if tp.key == tp_fn_entry.address:
            indent = '  ' * indents[thread]
            indents[thread] += 1
            trace_function(indent, '->', trace.data)
        elif tp.key == tp_fn_exit.address:
            indents[thread] -= 1
            if indents[thread] < 0:
                indents[thread] = 0
            indent = '  ' * indents[thread]
            trace_function(indent, '<-', trace.data)
        else:
            out_func('%s\n' % trace.format(bt_formatter))

def set_leak(val):
    gdb.parse_and_eval('memory::tracker_enabled=%s' % val)

def show_leak():
    tracker = gdb.parse_and_eval('memory::tracker')
    size_allocations = to_int(tracker['size_allocations'])
    allocations = tracker['allocations']
    # Build a list of allocations to be sorted lexicographically by call chain
    # and summarize allocations with the same call chain:
    percent='   ';
    gdb.write('Fetching data from qemu/osv: %s' % percent);
    gdb.flush();
    allocs = [];
    for i in range(size_allocations) :
        newpercent = '%2d%%' % round(100.0*i/(size_allocations-1));
        if newpercent != percent :
            percent = newpercent;
            gdb.write('\b\b\b%s' % newpercent);
            gdb.flush();
        a = allocations[i]
        addr = ulong(a['addr'])
        if addr == 0 :
            continue
        nbacktrace = to_int(a['nbacktrace'])
        backtrace = a['backtrace']
        callchain = []
        for j in range(nbacktrace) :
            callchain.append(ulong(backtrace[nbacktrace-1-j]))
        allocs.append((i, callchain))
    gdb.write('\n');

    gdb.write('Merging %d allocations by identical call chain... ' %
              len(allocs))
    gdb.flush();
    allocs.sort(key=lambda entry: entry[1])
    
    import collections
    Record = collections.namedtuple('Record',
                                    ['bytes', 'allocations', 'minsize',
                                     'maxsize', 'avgsize', 'minbirth',
                                     'maxbirth', 'avgbirth', 'callchain'])
    records = [];
    
    total_size = 0
    cur_n = 0
    cur_total_size = 0
    cur_total_seq = 0
    cur_first_seq = -1
    cur_last_seq = -1
    cur_max_size = -1
    cur_min_size = -1
    for k, alloc in enumerate(allocs) :
        i = alloc[0]
        callchain = alloc[1]
        seq = ulong(allocations[i]['seq'])
        size = ulong(allocations[i]['size'])
        total_size += size
        cur_n += 1
        cur_total_size += size
        cur_total_seq += seq
        if cur_first_seq<0 or seq<cur_first_seq :
            cur_first_seq = seq
        if cur_last_seq<0 or seq>cur_last_seq :
            cur_last_seq = seq
        if cur_min_size<0 or size<cur_min_size :
            cur_min_size = size
        if cur_max_size<0 or size>cur_max_size :
            cur_max_size = size
        # If the next entry has the same call chain, just continue summing
        if k!=len(allocs)-1 and callchain==allocs[k+1][1] :
            continue;
        # We're done with a bunch of allocations with same call chain:
        r = Record(bytes = cur_total_size,
                   allocations = cur_n,
                   minsize = cur_min_size,
                   maxsize = cur_max_size,
                   avgsize = cur_total_size/cur_n,
                   minbirth = cur_first_seq,
                   maxbirth = cur_last_seq,
                   avgbirth = cur_total_seq/cur_n,
                   callchain = callchain)
        records.append(r)
        cur_n = 0
        cur_total_size = 0
        cur_total_seq = 0
        cur_first_seq = -1
        cur_last_seq = -1
        cur_max_size = -1
        cur_min_size = -1
    gdb.write('generated %d records.\n' % len(records))
        
    # Now sort the records by total number of bytes
    records.sort(key=lambda r: r.bytes, reverse=True)

    gdb.write('\nAllocations still in memory at this time (seq=%d):\n\n' %
              tracker['current_seq'])
    for r in records :
        gdb.write('Found %d bytes in %d allocations [size ' % (r.bytes, r.allocations))
        if r.minsize != r.maxsize :
            gdb.write('%d/%.1f/%d' % (r.minsize, r.avgsize, r.maxsize))
        else :
            gdb.write('%d' % r.minsize)
        gdb.write(', birth ')
        if r.minbirth != r.maxbirth :
            gdb.write('%d/%.1f/%d' % (r.minbirth, r.avgbirth, r.maxbirth))
        else :
            gdb.write('%d' % r.minbirth)
        gdb.write(']\nfrom:\n')
        for f in reversed(r.callchain):
            si = syminfo(f)
            gdb.write('\t%s\n' % (si,))
        gdb.write('\n')

def drivers():
    drvman = gdb.lookup_global_symbol('hw::driver_manager::_instance').value()
    drivers = drvman['_drivers']
    return read_vector(drivers)

def show_virtio_driver(v):
    gdb.write('%s at %s\n' % (v.dereference().dynamic_type, v))
    vb = v.cast(virtio_driver_type.pointer())
    for qidx in range(0, to_int(vb['_num_queues'])):
        q = vb['_queues'][qidx]
        gdb.write('  queue %d at %s\n' % (qidx, q))
        avail_guest_idx = q['_avail']['_idx']['_M_i']
        avail_host_idx = q['_avail_event']['_M_i']
        gdb.write('    avail g=0x%x h=0x%x (%d)\n'
                  % (avail_host_idx, avail_guest_idx, avail_guest_idx - avail_host_idx))
        used_host_idx = q['_used']['_idx']['_M_i']
        used_guest_idx = q['_used_event']['_M_i']
        gdb.write('    used   h=0x%x g=0x%x (%d)\n'
                  % (used_host_idx, used_guest_idx, used_host_idx - used_guest_idx))
        used_flags = q['_used']['_flags']['_M_i']
        gdb.write('    used notifications: %s\n' %
                  ('disabled' if used_flags & 1 else 'enabled',))

class osv_trace(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace', gdb.COMMAND_USER, gdb.COMPLETE_COMMAND, True)
    def invoke(self, arg, from_tty):
        dump_trace(gdb.write)

class osv_trace_save(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace save', gdb.COMMAND_USER, gdb.COMPLETE_COMMAND, True)
    def invoke(self, arg, from_tty):
        if not arg:
            gdb.write('Missing argument. Usage: osv trace save <filename>\n')
            return

        gdb.write('Saving traces to %s ...\n' % arg)
        save_traces_to_file(arg)

class osv_trace_file(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv trace2file', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        fout = file("trace.txt", "wt")
        dump_trace(fout.write)
        fout.close()

class osv_leak(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class osv_leak_show(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak show', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        show_leak()

class osv_leak_on(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak on', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        set_leak('true')

class osv_leak_off(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv leak off', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        set_leak('false')

class osv_pagetable(gdb.Command):
    '''Commands for examining the page table'''
    def __init__(self):
        gdb.Command.__init__(self, 'osv pagetable', gdb.COMMAND_USER,
                             gdb.COMPLETE_COMMAND, True)

class osv_pagetable_walk(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv pagetable walk',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        addr = gdb.parse_and_eval(arg)
        addr = ulong(addr)
        ptep = ulong(gdb.lookup_symbol('mmu::page_table_root')[0].value().address)
        level = 4
        while level >= 0:
            ptep1 = phys_cast(ptep, ulong_type)
            pte = ulong(ptep1.dereference())
            gdb.write('%016x %016x\n' % (ptep, pte))
            if not pte & 1:
                break
            if level > 0 and pte & 0x80:
                break
            if level > 0:
                pte &= ~ulong(0x80)
            pte &= ~ulong(0x8000000000000fff)
            level -= 1
            ptep = pte + pt_index(addr, level) * 8

def runqueue(cpuid, node = None):
    if (node == None):
        cpus = gdb.lookup_global_symbol('sched::cpus').value()
        cpu = cpus['_M_impl']['_M_start'][cpuid]
        rq = cpu['runqueue']
        p = rq['data_']['node_plus_pred_']
        node = p['header_plus_size_']['header_']['parent_']

    if node:
        offset = gdb.parse_and_eval('(int)&((sched::thread *)0)->_runqueue_link');
        thread = node.cast(gdb.lookup_type('void').pointer()) - offset
        thread = thread.cast(gdb.lookup_type('sched::thread').pointer())

        for x in runqueue(cpuid, node['left_']):
            yield x

        yield thread

        for x in runqueue(cpuid, node['right_']):
            yield x

class osv_runqueue(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv runqueue',
                             gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        ncpus = gdb.parse_and_eval('sched::cpus._M_impl._M_finish - sched::cpus._M_impl._M_start');
        for cpu in range(ncpus) :
            gdb.write("CPU %d:\n" % cpu)
            for thread in runqueue(cpu):
                print('%d 0x%x %g' % (thread['_id'], ulong(thread), thread['_runtime']['_Rtt']))

class osv_info_virtio(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, 'osv info virtio', gdb.COMMAND_USER, gdb.COMPLETE_NONE)
    def invoke(self, arg, from_tty):
        for driver in drivers():
            if derived_from(driver.dereference().dynamic_type, virtio_driver_type):
                show_virtio_driver(driver)


osv()
osv_heap()
osv_memory()
osv_waiters()
osv_mmap()
osv_zfs()
osv_syms()
osv_info()
osv_info_threads()
osv_info_callouts()
osv_info_virtio()
osv_thread()
osv_thread_apply()
osv_thread_apply_all()
osv_trace()
osv_trace_save()
osv_trace_file()
osv_leak()
osv_leak_show()
osv_leak_on()
osv_leak_off()
osv_pagetable()
osv_pagetable_walk()
osv_runqueue()

setup_libstdcxx()
