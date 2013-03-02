import fnmatch
import os
import os.path
import re
import sys

includes = ['*.c', '*.h'] # for files only
# transform glob patterns to regular expressions
includes = r'|'.join([fnmatch.translate(x) for x in includes])

# lowest comes first
order_one = {}
order_one['SI_SUB_DUMMY'] = 0x0000000
order_one['SI_SUB_DONE'] = 0x0000001
order_one['SI_SUB_TUNABLES'] = 0x0700000
order_one['SI_SUB_COPYRIGHT'] = 0x0800001
order_one['SI_SUB_SETTINGS'] = 0x0880000
order_one['SI_SUB_MTX_POOL_STATIC'] = 0x0900000
order_one['SI_SUB_LOCKMGR'] = 0x0980000
order_one['SI_SUB_VM'] = 0x1000000
order_one['SI_SUB_KMEM'] = 0x1800000
order_one['SI_SUB_KVM_RSRC'] = 0x1A00000
order_one['SI_SUB_WITNESS'] = 0x1A80000
order_one['SI_SUB_MTX_POOL_DYNAMIC'] = 0x1AC0000
order_one['SI_SUB_LOCK'] = 0x1B00000
order_one['SI_SUB_EVENTHANDLER'] = 0x1C00000
order_one['SI_SUB_VNET_PRELINK'] = 0x1E00000
order_one['SI_SUB_KLD'] = 0x2000000
order_one['SI_SUB_CPU'] = 0x2100000
order_one['SI_SUB_RACCT'] = 0x2110000
order_one['SI_SUB_RANDOM'] = 0x2120000
order_one['SI_SUB_KDTRACE'] = 0x2140000
order_one['SI_SUB_MAC'] = 0x2180000
order_one['SI_SUB_MAC_POLICY'] = 0x21C0000
order_one['SI_SUB_MAC_LATE'] = 0x21D0000
order_one['SI_SUB_VNET'] = 0x21E0000
order_one['SI_SUB_INTRINSIC'] = 0x2200000
order_one['SI_SUB_VM_CONF'] = 0x2300000
order_one['SI_SUB_DDB_SERVICES'] = 0x2380000
order_one['SI_SUB_RUN_QUEUE'] = 0x2400000
order_one['SI_SUB_KTRACE'] = 0x2480000
order_one['SI_SUB_OPENSOLARIS'] = 0x2490000
order_one['SI_SUB_CYCLIC'] = 0x24A0000
order_one['SI_SUB_AUDIT'] = 0x24C0000
order_one['SI_SUB_CREATE_INIT'] = 0x2500000
order_one['SI_SUB_SCHED_IDLE'] = 0x2600000
order_one['SI_SUB_MBUF'] = 0x2700000
order_one['SI_SUB_INTR'] = 0x2800000
order_one['SI_SUB_SOFTINTR'] = 0x2800001
order_one['SI_SUB_ACL'] = 0x2900000
order_one['SI_SUB_DEVFS'] = 0x2F00000
order_one['SI_SUB_INIT_IF'] = 0x3000000
order_one['SI_SUB_NETGRAPH'] = 0x3010000
order_one['SI_SUB_DTRACE'] = 0x3020000
order_one['SI_SUB_DTRACE_PROVIDER'] = 0x3048000
order_one['SI_SUB_DTRACE_ANON'] = 0x308C000
order_one['SI_SUB_DRIVERS'] = 0x3100000
order_one['SI_SUB_CONFIGURE'] = 0x3800000
order_one['SI_SUB_VFS'] = 0x4000000
order_one['SI_SUB_CLOCKS'] = 0x4800000
order_one['SI_SUB_CLIST'] = 0x5800000
order_one['SI_SUB_SYSV_SHM'] = 0x6400000
order_one['SI_SUB_SYSV_SEM'] = 0x6800000
order_one['SI_SUB_SYSV_MSG'] = 0x6C00000
order_one['SI_SUB_P'] = 0x6E00000
order_one['SI_SUB_PSEUDO'] = 0x7000000
order_one['SI_SUB_EXEC'] = 0x7400000
order_one['SI_SUB_PROTO_BEGIN'] = 0x8000000
order_one['SI_SUB_PROTO_IF'] = 0x8400000
order_one['SI_SUB_PROTO_DOMAININIT'] = 0x8600000
order_one['SI_SUB_PROTO_DOMAIN'] = 0x8800000
order_one['SI_SUB_PROTO_IFATTACHDOMAIN'] = 0x8800001
order_one['SI_SUB_PROTO_END'] = 0x8ffffff
order_one['SI_SUB_KPROF'] = 0x9000000
order_one['SI_SUB_KICK_SCHEDULER'] = 0xa000000
order_one['SI_SUB_INT_CONFIG_HOOKS'] = 0xa800000
order_one['SI_SUB_ROOT_CONF'] = 0xb000000
order_one['SI_SUB_DUMP_CONF'] = 0xb200000
order_one['SI_SUB_RAID'] = 0xb380000
order_one['SI_SUB_SWAP'] = 0xc000000
order_one['SI_SUB_INTRINSIC_POST'] = 0xd000000
order_one['SI_SUB_SYSCALLS'] = 0xd800000
order_one['SI_SUB_VNET_DONE'] = 0xdc00000
order_one['SI_SUB_KTHREAD_INIT'] = 0xe000000
order_one['SI_SUB_KTHREAD_PAGE'] = 0xe400000
order_one['SI_SUB_KTHREAD_VM'] = 0xe800000
order_one['SI_SUB_KTHREAD_BUF'] = 0xea00000
order_one['SI_SUB_KTHREAD_UPDATE'] = 0xec00000
order_one['SI_SUB_KTHREAD_IDLE'] = 0xee00000
order_one['SI_SUB_SMP'] = 0xf000000
order_one['SI_SUB_RACCTD'] = 0xf100000
order_one['SI_SUB_RUN_SCHEDULER'] = 0xfffffff
order_one['PFIL_SYSINIT_ORDER'] = order_one['SI_SUB_PROTO_BEGIN']


order_two = {}
order_two["SI_ORDER_FIRST"] = 0x0000000
order_two["SI_ORDER_SECOND"] = 0x0000001
order_two["SI_ORDER_THIRD"] = 0x0000002
order_two["SI_ORDER_FOURTH"] = 0x0000003
order_two["SI_ORDER_MIDDLE"] = 0x1000000
order_two["SI_ORDER_ANY"] = 0xfffffff
order_two["PFIL_VNET_ORDER"] = order_two["SI_ORDER_FIRST"] + 2
 
matches = []

def extract_matches(mt):
    global order_one
    global order_two
    global matches

    for m in mt:
        l = [x.strip() for x in m]
        (name,o1,o2,func,udata) = l

        if (not order_one.has_key(o1)):
            print o1, "is not in order_one dict"
            sys.exit(1)

        if (not order_two.has_key(o2)):
            print o2, "is not in order_two dict"
            sys.exit(1)

        matches += [["%s(%s);" % (func, udata), order_one[o1], order_two[o2]]]
   
def handle_file(fname):

    # Extract SYSINIT and VNET_SYSINIT macro variables from file
    txt = file(fname).read()
    f = re.findall("^VNET_SYSINIT\((.*?),(.*?),(.*?),(.*?),(.*?)\)", txt, re.I + re.M + re.S)
    g = re.findall("^SYSINIT\((.*?),(.*?),(.*?),(.*?),(.*?)\)", txt, re.I + re.M + re.S)
    
    extract_matches(f)
    extract_matches(g)

def process_files(top):

    for root, dirs, files in os.walk(top):

        # include files
        files = [os.path.join(root, f) for f in files]
        files = [f for f in files if re.match(includes, f)]

        for fname in files:
            handle_file(fname)

#
# MAIN
#

if (__name__ == "__main__"):

    # Extract matches from the bsd tree
    process_files(sys.argv[1])

    # Order matches
    matches.sort(key = lambda o: o[1]*(2**32) + o[2])
    print "Debug Print:"
    for o in matches:
        print "%s    %s,%s" % (o[0], hex(o[1]), hex(o[2]))

    print "\nCopy & Paste Me:"
    for o in matches:
        print o[0]

    
