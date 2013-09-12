from collections import Counter
import gdb
import re

print("-=[ Mem0ry Allocations Analyz3r ]=-")

trace_records = file("trace.txt", "rt").readlines()

def is_malloc(x):
    return x.find("malloc ") > 0

def is_free(x):
    return x.find("memory_free ") > 0

def get_len(x):
    return re.findall("len=(.*)\n", x)[0]

def get_buf(x):
    return re.findall("buf=(0x................)", x)[0]

def is_page_alloc(x):
    pass

def is_page_free(x):
    pass

def get_page(x):
    pass

# buf -> [(size, is_freed), ...]
mallocs = {}

def process_records():
    global mallocs
    global trace_records

    for l in trace_records:
        try:
            #
            # Maintain a hash dictionary that maps between buffer address and a list of
            # tuples, each describing the size of the buffer and whether it had been
            # freed.
            #
            if (is_malloc(l)):
                buf = get_buf(l)
                t = [get_len(l), False]
                if (mallocs.has_key(buf)):
                    if (mallocs[buf][-1][1] == False):
                        print "Buffer %s was already allocated." % buf
                    mallocs[buf] += [t]
                else:
                    mallocs[buf] = [t]
            
            elif (is_free(l)):
                buf = get_buf(l)
                # check if buffer had been allocated
                if (not mallocs.has_key(buf)):
                    # print "Buffer %s never been allocated." % buf
                    pass
                else:
                    # check if buffer already freed 
                    if (mallocs[buf][-1][1] == True):
                        print "Buffer %s buf already been freed." % buf
                    else:
                       mallocs[buf][-1][1] = True 
        except:
            print "Problem parsing line: '%s'" % l
            raise


def process_counts():
    "Group number of allocations and deallocations by size"
    global mallocs

    sizes = {}
    total = 0

    print "Calculating size histograms..."

    for buf in mallocs:
        for desc in mallocs[buf]:
            size = desc[0]
            freed = desc[1]
            if (not sizes.has_key(size)):
                sizes[size] = {'allocs':0, 'frees':0}

            sizes[size]['allocs'] += 1
            if (freed):
                sizes[size]['frees'] += 1

    sizes = sorted(sizes.iteritems(), key=lambda k: (k[1]['allocs']-k[1]['frees']) * int(k[0]))

    # print buffers
    for sz in sizes:
        unfreed = (sz[1]['allocs'] - sz[1]['frees']) * int(sz[0])
        print sz[0], sz[1], "Unallocated: %d bytes" % unfreed
        total += unfreed 
        
    print "Total unfreed: %d bytes" % total
    

if __name__ == "__main__":
    process_records()
    process_counts()    

